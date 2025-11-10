/*
 * This file is part of libqbox
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _LIBQBOX_COMPONENTS_MCIPS_PLUGIN_H
#define _LIBQBOX_COMPONENTS_MCIPS_PLUGIN_H

#include "libqemu-plugin.h"
#include <sync_window.h>
#include <async_event.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <sstream>

class McipsPlugin : public LibQemuPlugin
{
    SCP_LOGGER();
    SC_HAS_PROCESS(McipsPlugin);

private:
    enum vCPUTimeStatus { IDLE, PAUSED, RUNNING };

    struct vCPUTime {
        uint64_t index;
        uint64_t insn_per_second;
        uint64_t delta_insn;
        sc_core::sc_time cpu_time;
        vCPUTimeStatus cpu_execution_status;
    };

    uint64_t m_global_quantum; // quantum in nanoseconds; used as the instruction count limit
    int m_num_vcpus{ 0 };      // cached CPU count (set in end_of_elaboration, may grow in vcpu_init)
    void* m_time_handle;       // handle returned by QEMU to let us control time
    sc_core::sc_sync_window<sc_core::sc_sync_policy_tlm_quantum> m_sync_sc; // SystemC sync window
    sc_core::sc_sync_window<sc_core::sc_sync_policy_tlm_quantum>::window
        sc_current_window;        // last window received from SystemC
    sc_core::sc_time m_quantum;   // copy of the TLM global quantum
    sc_core::sc_time m_qemu_time; // main QEMU simulation time (only changed while holding m_mcips_mutex)
    /* CPU currently driving m_qemu_time. Read lock-free by get_qemu_clock() (acquire/release). */
    std::atomic<vCPUTime*> m_active_vcpu;
    qemu_plugin_scoreboard* m_vcpus_scoreboard; // one vCPUTime entry per CPU index

    /* Nanosecond shadow of m_qemu_time; sc_time is not thread-safe. */
    std::atomic<int64_t> m_qemu_time_ns{ 0 };

    /* Shutdown flag; all entry points return immediately when set. */
    std::atomic<bool> m_shutdown{ false };

    /* Counts in-flight receive_window_cb() calls; shutdown waits for zero. */
    std::atomic<int> m_inflight_cb{ 0 };

    /* Protects shared state across vCPU and SystemC threads. */
    std::mutex m_mcips_mutex;

    /* Fires a SystemC event to keep time moving when all CPUs are idle (the "idle pump"),
     * and doubles as the iothread-livelock watchdog kick. Notified by vcpu_idle() and
     * get_qemu_clock(); vcpu_resume() lets it lapse once a CPU is active again. */
    gs::async_event m_idle_tick;

    /* False until the first vCPU is initialized; while false, get_qemu_clock() kicks the
     * idle pump so time advances during startup before any vCPU exists. */
    std::atomic<bool> m_first_vcpu_initialized{ false };

    /* Iothread-livelock watchdog state. Wall-clock gated (1s) so sub-second
     * unit tests never trip it. */
    std::atomic<int64_t> m_wd_last_clock_ns{ 0 };
    std::atomic<int64_t> m_wd_freeze_start_ns{ 0 };
    sc_core::sc_time m_wd_last_tick_qemu_time{ sc_core::SC_ZERO_TIME }; // SC-side, under m_mcips_mutex
    static constexpr int64_t kWdFreezeThresholdNs = 1'000'000'000LL;

public:
    McipsPlugin(const sc_core::sc_module_name& nm, qemu::LibQemu& inst)
        : LibQemuPlugin(nm, inst)
        , m_sync_sc("m_sync_sc")
        , m_global_quantum(0)
        , m_time_handle(nullptr)
        , sc_current_window(sc_core::sc_sync_window<sc_core::sc_sync_policy_tlm_quantum>::zero_window)
        , m_quantum(sc_core::SC_ZERO_TIME)
        , m_qemu_time(sc_core::SC_ZERO_TIME)
        , m_active_vcpu(nullptr)
        , m_vcpus_scoreboard(nullptr)
        , m_idle_tick(false)
    {
        SC_METHOD(idle_tick_method);
        dont_initialize();
        sensitive << m_idle_tick;
    }

    /**
     * @brief SC_METHOD woken by m_idle_tick. When a CPU is active it does iothread-livelock
     *        recovery (bumps qemu_time if it has frozen). When all CPUs are idle it does nothing:
     *        just being scheduled was enough to step the SystemC kernel forward (the "idle pump").
     */
    void idle_tick_method()
    {
        std::unique_lock<std::mutex> lock(m_mcips_mutex);

        // Watchdog recovery: kicked by get_qemu_clock when m_qemu_time has been frozen >1s wall.
        // If qemu_time hasn't advanced since the last tick, bump it by one quantum so the iothread's
        // next gt_recalc_timer reads a fresh deadline and its ppoll goes back to sleep.
        auto* active = m_active_vcpu.load(std::memory_order_relaxed);
        if (active) {
            const sc_core::sc_time current_qemu_time = qemu_time_now(active);
            if (current_qemu_time == m_wd_last_tick_qemu_time && m_quantum > sc_core::SC_ZERO_TIME) {
                m_qemu_time += m_quantum;
                sync_qemu_time_ns();
                active->cpu_time += m_quantum;
                set_systemc_window();
                m_idle_tick.notify(m_quantum);
            }
            m_wd_last_tick_qemu_time = current_qemu_time;
        }
    }

    /**
     * @brief Shared shutdown logic used by both the destructor and end_of_simulation().
     *
     * Stops the idle pump, marks shutdown, clears the active CPU, waits for
     * any in-flight receive_window_cb to finish, and detaches the sync window.
     * Safe to call more than once (subsequent calls are no-ops).
     */
    void shutdown_cleanup()
    {
        if (m_shutdown.load(std::memory_order_seq_cst)) {
            return; // already shut down
        }

        m_shutdown.store(true, std::memory_order_seq_cst); // must be seq_cst before inflight drain
        m_active_vcpu.store(nullptr, std::memory_order_release);

        shutdown_bridge();

        /* Wait for any in-flight receive_window_cb() to finish. */
        while (m_inflight_cb.load(std::memory_order_seq_cst) > 0) {
            std::this_thread::yield();
        }

        detach_sync_window();
    }

    ~McipsPlugin() { shutdown_cleanup(); }

    /** @brief Called by SystemC at end of simulation; stops all QEMU callbacks. */
    void end_of_simulation() override { shutdown_cleanup(); }

    /** @brief Set up the plugin, register QEMU callbacks, and send the first sync window. */
    void end_of_elaboration() override
    {
        LibQemuPlugin::end_of_elaboration();
        m_vcpus_scoreboard = m_inst.plugin_api().qemu_plugin_scoreboard_new(sizeof(vCPUTime));
        sc_assert(m_vcpus_scoreboard);

        m_time_handle = const_cast<void*>(m_inst.plugin_api().qemu_plugin_request_time_control(m_id));
        sc_assert(m_time_handle);

        m_quantum = tlm_utils::tlm_quantumkeeper::get_global_quantum();
        m_global_quantum = static_cast<uint64_t>(std::floor(m_quantum.to_seconds() * NSEC_IN_ONE_SEC));

        sc_current_window = { sc_core::SC_ZERO_TIME, m_quantum };
        if (m_sync_sc.is_attached()) {
            m_sync_sc.async_set_window(sc_current_window);
        } else {
            SCP_FATAL(()) << "Window must be attached before calling async_set_window()";
            sc_assert(false);
        }
        m_num_vcpus = m_inst.plugin_api().qemu_plugin_num_vcpus();
        m_sync_sc.register_sync_cb(std::bind(&McipsPlugin::receive_window_cb, this, std::placeholders::_1));

        /* Every callback is registered directly with the plugin API as a
         * capture-less lambda (which decays to the C function pointer the API
         * expects). Each forwards through LibQemuPlugin::dispatch_userdata for
         * shutdown synchronization against the leaked PluginHandle, a raw
         * `this` would dangle once this SystemC module is destroyed while QEMU
         * threads still have callbacks in flight. */
        m_inst.plugin_api().qemu_plugin_register_vcpu_tb_trans_cb(
            m_id,
            [](qemu_plugin_tb* tb, void* userdata) {
                LibQemuPlugin::dispatch_userdata(
                    userdata, [&](LibQemuPlugin* p) { static_cast<McipsPlugin*>(p)->vcpu_tb_trans(tb); });
            },
            handle_as_userdata());
        m_inst.plugin_api().qemu_plugin_register_vcpu_init_cb(
            m_id,
            [](unsigned int cpu_index, void* userdata) {
                LibQemuPlugin::dispatch_userdata(
                    userdata, [&](LibQemuPlugin* p) { static_cast<McipsPlugin*>(p)->vcpu_init(cpu_index); });
            },
            handle_as_userdata());
        m_inst.plugin_api().qemu_plugin_register_vcpu_resume_cb(
            m_id,
            [](unsigned int cpu_index, void* userdata) {
                LibQemuPlugin::dispatch_userdata(
                    userdata, [&](LibQemuPlugin* p) { static_cast<McipsPlugin*>(p)->vcpu_resume(cpu_index); });
            },
            handle_as_userdata());
        m_inst.plugin_api().qemu_plugin_register_vcpu_idle_cb(
            m_id,
            [](unsigned int cpu_index, void* userdata) {
                LibQemuPlugin::dispatch_userdata(
                    userdata, [&](LibQemuPlugin* p) { static_cast<McipsPlugin*>(p)->vcpu_idle(cpu_index); });
            },
            handle_as_userdata());
        m_inst.plugin_api().qemu_plugin_register_time_cb(
            m_time_handle,
            [](void* userdata) -> int64_t {
                int64_t result = static_cast<int64_t>(sc_core::sc_time_stamp().to_seconds() * NSEC_IN_ONE_SEC);
                LibQemuPlugin::dispatch_userdata(userdata, [&](LibQemuPlugin* p) {
                    result = static_cast<McipsPlugin*>(p)->get_qemu_clock(nullptr);
                });
                return result;
            },
            handle_as_userdata());
    }

    /** @brief Helper function to get vCPU time structure from scoreboard */
    vCPUTime* get_vcpu(unsigned int cpu_index)
    {
        return reinterpret_cast<vCPUTime*>(
            m_inst.plugin_api().qemu_plugin_scoreboard_find(m_vcpus_scoreboard, cpu_index));
    }

    void attach_sync_window()
    {
        if (!m_sync_sc.is_attached()) m_sync_sc.attach();
    }

    void detach_sync_window()
    {
        if (m_sync_sc.is_attached()) m_sync_sc.detach(m_qemu_time);
    }

    /**
     * @brief Set the instructions per second for a specific vCPU
     * @param cpu_index Index of the vCPU
     * @param insn_per_second Instructions per second value to set
     * @return true if successful, false if vCPU not found in scoreboard
     */
    bool set_vcpu_insn_per_second(unsigned int cpu_index, uint64_t insn_per_second)
    {
        if (insn_per_second == 0) {
            SCP_FATAL(()) << "insn_per_second must be > 0 (cpu_" << cpu_index << ")";
            return false;
        }

        const unsigned int n = static_cast<unsigned int>(m_inst.plugin_api().qemu_plugin_num_vcpus());
        if (cpu_index >= n) {
            return false;
        }
        if (n > static_cast<unsigned int>(m_num_vcpus)) m_num_vcpus = static_cast<int>(n);

        vCPUTime* vcpu = get_vcpu(cpu_index);
        if (!vcpu) {
            return false;
        }

        vcpu->insn_per_second = insn_per_second;
        return true;
    }

    /** @brief Copy m_qemu_time into the atomic nanosecond field. Must hold m_mcips_mutex when calling. */
    void sync_qemu_time_ns()
    {
        m_qemu_time_ns.store(static_cast<int64_t>(m_qemu_time.to_seconds() * NSEC_IN_ONE_SEC),
                             std::memory_order_release);
    }

    /** @brief Time represented by the in-flight instructions in delta_insn. */
    static sc_core::sc_time cpu_delta_time(const vCPUTime* vcpu)
    {
        sc_assert(vcpu && "cpu_delta_time called with null vCPU");
        return sc_core::sc_time(static_cast<double>(vcpu->delta_insn) / vcpu->insn_per_second, sc_core::SC_SEC);
    }

    /** @brief Current time for a specific CPU (base + in-flight delta). Must hold m_mcips_mutex when calling. */
    sc_core::sc_time cpu_time_now(const vCPUTime* vcpu)
    {
        sc_assert(vcpu && "cpu_time_now called with null vCPU");
        if (vcpu->cpu_execution_status == IDLE) {
            return vcpu->cpu_time;
        }
        return vcpu->cpu_time + cpu_delta_time(vcpu);
    }

    /**
     * @brief Current QEMU time (base + active CPU's in-flight delta).
     *
     * When called under m_mcips_mutex, prefer the overload that takes an
     * explicit @p active pointer to avoid a redundant atomic load.
     */
    sc_core::sc_time qemu_time_now() { return qemu_time_now(m_active_vcpu.load(std::memory_order_acquire)); }

    /** @brief Overload for callers that already hold m_mcips_mutex and have the active pointer. */
    sc_core::sc_time qemu_time_now(const vCPUTime* active)
    {
        if (!active) return m_qemu_time;
        return m_qemu_time + cpu_delta_time(active);
    }

    /** @brief Find the CPU that is furthest behind in time. */
    vCPUTime* slowest_none_idle_cpu()
    {
        vCPUTime* slowest_cpu = nullptr;
        sc_core::sc_time min_time = sc_core::SC_ZERO_TIME;

        for (int i = 0; i < m_num_vcpus; i++) {
            auto* vcpu = get_vcpu(i);
            if (vcpu->cpu_execution_status == IDLE) continue;

            const sc_core::sc_time current_cpu_time = cpu_time_now(vcpu);
            if (!slowest_cpu || current_cpu_time < min_time) {
                min_time = current_cpu_time;
                slowest_cpu = vcpu;
            }
        }
        return slowest_cpu;
    }

    /**
     * @brief Pick the first non-idle CPU and make it the active one.
     *
     * @return The chosen CPU, or nullptr if every CPU is idle.
     */
    vCPUTime* select_active_vcpu()
    {
        for (int i = 0; i < m_num_vcpus; i++) {
            vCPUTime* vcpu = get_vcpu(i);
            if (vcpu->cpu_execution_status != IDLE) {
                m_active_vcpu.store(vcpu, std::memory_order_release);
                SCP_DEBUG(()) << "cpu_" << vcpu->index << " is the new active cpu";
                return vcpu;
            }
        }
        m_active_vcpu.store(nullptr, std::memory_order_release);
        return nullptr;
    }

    /** @brief Pause the CPU if it has run too far ahead of the allowed limit. m_mcips_mutex must be held. */
    void pause_if_ahead(vCPUTime* vcpu, const vCPUTime* active, sc_core::sc_time current_qemu_time,
                        sc_core::sc_time threshold)
    {
        if (vcpu->cpu_execution_status != RUNNING) return;

        const sc_core::sc_time vcpu_time = cpu_time_now(vcpu);
        if ((vcpu == active && current_qemu_time > threshold) || (vcpu != active && vcpu_time > current_qemu_time)) {
            vcpu->cpu_execution_status = PAUSED;
            m_inst.plugin_api().qemu_plugin_cpu_request_pause(static_cast<unsigned int>(vcpu->index));
            SCP_DEBUG(()) << "cpu_" << vcpu->index << " paused, time=" << vcpu_time;
        }
    }

    /** @brief Resume paused CPUs that are still within the allowed time limit. m_mcips_mutex must be held. */
    void resume_if_behind(const vCPUTime* active, sc_core::sc_time current_qemu_time, sc_core::sc_time threshold)
    {
        for (int i = 0; i < m_num_vcpus; i++) {
            auto* vcpu = get_vcpu(i);
            if (vcpu->cpu_execution_status != PAUSED) continue;
            const sc_core::sc_time vcpu_time = cpu_time_now(vcpu);
            if ((vcpu == active && current_qemu_time <= threshold) ||
                (vcpu != active && vcpu_time <= current_qemu_time)) {
                vcpu->cpu_execution_status = RUNNING;
                m_inst.plugin_api().qemu_plugin_cpu_resume(static_cast<unsigned int>(vcpu->index));
                SCP_DEBUG(()) << "cpu_" << vcpu->index << " resumed, time=" << vcpu_time;
            }
        }
    }

    /**
     * @brief Compute the time threshold that determines which CPUs may run.
     *
     * The threshold is the lesser of (a) the end of the current SystemC window
     * and (b) the slowest active CPU's time plus one quantum.
     * m_mcips_mutex must be held.
     *
     * @return The threshold, or SC_ZERO_TIME if there are no active CPUs.
     */
    sc_core::sc_time compute_threshold()
    {
        const vCPUTime* slowest_cpu = slowest_none_idle_cpu();
        if (!slowest_cpu) return sc_core::SC_ZERO_TIME;
        return std::min(sc_current_window.to, cpu_time_now(slowest_cpu) + m_quantum);
    }

    /**
     * @brief Rebalance CPUs and notify SystemC. m_mcips_mutex must be held.
     *
     * Computes the current threshold, pauses @p vcpu if it is ahead,
     * resumes any paused CPUs that are behind, and sends a new window.
     * If @p vcpu is nullptr only resume + window are performed.
     */
    void rebalance_and_sync(vCPUTime* vcpu = nullptr)
    {
        const auto* active = m_active_vcpu.load(std::memory_order_relaxed);

        if (!active) {
            SCP_DEBUG(()) << "rebalance_and_sync(no-active): all-idle, detaching window";
            detach_sync_window();
            return;
        }

        SCP_DEBUG(()) << "rebalance_and_sync: active cpu present";
        const sc_core::sc_time threshold = compute_threshold();
        if (threshold == sc_core::SC_ZERO_TIME) {
            SCP_DEBUG(()) << "rebalance_and_sync: threshold=0 with active cpu, skipping";
            return;
        }

        const sc_core::sc_time current_qemu_time = qemu_time_now(active);
        if (vcpu) {
            pause_if_ahead(vcpu, active, current_qemu_time, threshold);
        }
        resume_if_behind(active, current_qemu_time, threshold);
        set_systemc_window();
    }

    /**
     * @brief Set the SystemC synchronization window
     * @param custom_window Optional custom window to set; if nullptr, calculates window from current QEMU time
     */
    void set_systemc_window(
        const sc_core::sc_sync_window<sc_core::sc_sync_policy_tlm_quantum>::window* custom_window = nullptr)
    {
        if (m_sync_sc.is_attached()) {
            if (custom_window) {
                SCP_DEBUG(()) << "set_systemc_window::custom_window.from= " << custom_window->from
                              << ", custom_window.to= " << custom_window->to;
                m_sync_sc.async_set_window(*custom_window);
            } else {
                sc_core::sc_time current_qemu_time = qemu_time_now();
                SCP_DEBUG(()) << "set_systemc_window::qemu_cpu_time_now = " << current_qemu_time
                              << ", sc_current_window.from= " << sc_current_window.from
                              << ", sc_current_window.to= " << sc_current_window.to;
                m_sync_sc.async_set_window({ current_qemu_time, (current_qemu_time + m_quantum) });
            }
        } else {
            SCP_INFO(()) << "set_systemc_window: window not attached, skipping async_set_window()";
        }
    }

    /**
     * @brief Called by SystemC when a new time window is ready.
     *
     * This runs on the SystemC thread. It only takes the m_mcips_mutex.
     * Saves the new window, detaches if no CPU is active, resumes the active
     * CPU if the new window gives it room, then sends a new window back.
     */
    void receive_window_cb(const sc_core::sc_sync_window<sc_core::sc_sync_policy_tlm_quantum>::window& sc_w)
    {
        if (m_shutdown.load(std::memory_order_seq_cst)) return;

        m_inflight_cb.fetch_add(1, std::memory_order_seq_cst);
        if (m_shutdown.load(std::memory_order_seq_cst)) {
            m_inflight_cb.fetch_sub(1, std::memory_order_seq_cst);
            return;
        }

        std::lock_guard<std::mutex> lock(m_mcips_mutex);
        sc_current_window = sc_w;

        auto* active = m_active_vcpu.load(std::memory_order_relaxed);
        SCP_DEBUG(()) << "receive_window_cb: qemu_time=" << qemu_time_now(active) << ", sc_window=["
                      << sc_current_window.from << ", " << sc_current_window.to << "]";

        if (!active) {
            SCP_DEBUG(()) << "receive_window_cb(no-active): calling rebalance_and_sync";
            rebalance_and_sync();
            m_inflight_cb.fetch_sub(1, std::memory_order_seq_cst);
            return;
        }

        if (active->cpu_execution_status == PAUSED) {
            const sc_core::sc_time threshold = compute_threshold();
            if (threshold != sc_core::SC_ZERO_TIME && qemu_time_now(active) <= threshold) {
                active->cpu_execution_status = RUNNING;
                m_inst.plugin_api().qemu_plugin_cpu_resume(static_cast<unsigned int>(active->index));
                SCP_DEBUG(()) << "receive_window_cb: resumed active cpu";
            }
        }

        set_systemc_window();
        m_inflight_cb.fetch_sub(1, std::memory_order_seq_cst);
    }

    /** @brief Called under BQL before simulation starts; does not hold m_mcips_mutex. */
    void vcpu_init(unsigned int cpu_index)
    {
        if (m_shutdown.load(std::memory_order_acquire)) return;

        const int current = m_inst.plugin_api().qemu_plugin_num_vcpus();
        if (current > m_num_vcpus) m_num_vcpus = current;

        vCPUTime* vcpu = get_vcpu(cpu_index);
        vcpu->index = cpu_index;
        if (vcpu->insn_per_second == 0) {
            vcpu->insn_per_second = 1'000'000'000;
        }
        vcpu->delta_insn = 0;
        vcpu->cpu_time = sc_core::SC_ZERO_TIME;
        vcpu->cpu_execution_status = RUNNING;

        if (m_active_vcpu.load(std::memory_order_relaxed) == nullptr) {
            m_active_vcpu.store(vcpu, std::memory_order_release);
        }

        m_first_vcpu_initialized.store(true, std::memory_order_release);
    }

    /**
     * @brief Called when a CPU has run its full instruction quota.
     * Adds the time used, pauses/resumes CPUs as needed, and sends a new window.
     * Takes and releases m_mcips_mutex inside.
     */
    void cpu_end_delta_quota(vCPUTime* vcpu)
    {
        std::lock_guard<std::mutex> lock(m_mcips_mutex);

        if (vcpu->cpu_execution_status != RUNNING) {
            return;
        }

        const sc_core::sc_time delta_time = cpu_delta_time(vcpu);
        const auto* active = m_active_vcpu.load(std::memory_order_relaxed);
        sc_assert(active && "cpu_end_delta_quota called but no active CPU");

        const sc_core::sc_time old_qemu_time = m_qemu_time;
        if (active == vcpu) {
            m_qemu_time += delta_time;
            sync_qemu_time_ns();
            SCP_DEBUG(()) << "cpu_" << vcpu->index << " completed quantum, qemu_time: " << old_qemu_time << " -> "
                          << m_qemu_time << ", delta=" << delta_time << ", sc_time=" << sc_core::sc_time_stamp();
        }
        vcpu->cpu_time += delta_time;
        vcpu->delta_insn = 0;

        rebalance_and_sync(vcpu);
    }

    /** @brief Called when a CPU has run at least global_quantum instructions. */
    void vcpu_tb_exec_cond(unsigned int cpu_index, void* /*udata*/)
    {
        if (m_shutdown.load(std::memory_order_acquire)) return;

        vCPUTime* vcpu = get_vcpu(cpu_index);
        if (vcpu->cpu_execution_status != RUNNING) {
            return;
        }

        sc_assert(vcpu->delta_insn >= m_global_quantum && "TB exec condition fired but quota not met");
        cpu_end_delta_quota(vcpu);
    }

    /** @brief Called when QEMU translates a block, adds instruction counting and quota check. */
    void vcpu_tb_trans(qemu_plugin_tb* tb)
    {
        const size_t n_insns = m_inst.plugin_api().qemu_plugin_tb_n_insns(tb);
        qemu_plugin_u64 delta_insn = qemu_plugin_scoreboard_u64_in_struct(m_vcpus_scoreboard, vCPUTime, delta_insn);

        m_inst.plugin_api().qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                                                              delta_insn, n_insns);

        m_inst.plugin_api().qemu_plugin_register_vcpu_tb_exec_cond_cb(
            tb,
            /* Registered per translated block (not in end_of_elaboration), but
             * still a capture-less lambda for consistency with the others. */
            [](unsigned int cpu_index, void* userdata) {
                LibQemuPlugin::dispatch_userdata(userdata, [&](LibQemuPlugin* p) {
                    static_cast<McipsPlugin*>(p)->vcpu_tb_exec_cond(cpu_index, userdata);
                });
            },
            QEMU_PLUGIN_CB_NO_REGS, QEMU_PLUGIN_COND_GE, delta_insn, m_global_quantum, handle_as_userdata());
    }

    /**
     * @brief Called when a CPU goes idle (QEMU holds BQL; we additionally take m_mcips_mutex).
     *
     * RUNNING → IDLE: save the time used, pick a new active CPU or start the idle pump.
     * Takes m_mcips_mutex to protect shared McipsPlugin state.
     */
    void vcpu_idle(unsigned int cpu_index)
    {
        if (m_shutdown.load(std::memory_order_acquire)) return;

        std::lock_guard<std::mutex> lock(m_mcips_mutex);
        SCP_DEBUG(()) << "vcpu_idle callback for cpu_" << cpu_index;
        vCPUTime* vcpu = get_vcpu(cpu_index);

        switch (vcpu->cpu_execution_status) {
        case RUNNING: {
            SCP_DEBUG(()) << "cpu_" << cpu_index << " RUNNING → IDLE";
            vcpu->cpu_execution_status = IDLE;

            const auto* active = m_active_vcpu.load(std::memory_order_relaxed);
            if (vcpu == active) {
                SCP_DEBUG(()) << "cpu_" << cpu_index << " was active, looking for another cpu to take over";

                const sc_core::sc_time delta = cpu_delta_time(vcpu);
                m_qemu_time += delta;
                sync_qemu_time_ns();
                vcpu->cpu_time += delta;

                vCPUTime* new_active = select_active_vcpu();

                if (!new_active) {
                    if (!m_shutdown.load(std::memory_order_acquire)) {
                        m_idle_tick.notify(m_quantum);
                        SCP_DEBUG(()) << "[idle_pump] NOTIFY from vcpu_idle (cpu_" << cpu_index << ")";
                    }
                    detach_sync_window();
                } else {
                    new_active->cpu_time = m_qemu_time;
                    new_active->delta_insn = 0;

                    if (new_active->cpu_execution_status == PAUSED) {
                        new_active->cpu_execution_status = RUNNING;
                        m_inst.plugin_api().qemu_plugin_cpu_resume(static_cast<unsigned int>(new_active->index));
                    }
                }
            }
            vcpu->delta_insn = 0;
            rebalance_and_sync();
            break;
        }
        case PAUSED:
            SCP_DEBUG(()) << "vcpu_idle: cpu_" << cpu_index << " already PAUSED";
            break;

        case IDLE:
            SCP_DEBUG(()) << "vcpu_idle: cpu_" << cpu_index << " already IDLE";
            break;

        default:
            SCP_FATAL(()) << "vcpu_idle: invalid execution status for cpu_" << cpu_index;
            sc_assert(false);
            break;
        }
    }

    /**
     * @brief Called when a CPU wakes up (QEMU holds BQL; we additionally take m_mcips_mutex).
     *
     * IDLE → RUNNING: first CPU to wake re-attaches the sync window.
     * PAUSED → RUNNING: only if the CPU is still within the allowed time range.
     * RUNNING: nothing to do (plugin already set it to RUNNING earlier).
     * Takes m_mcips_mutex to protect shared McipsPlugin state.
     */
    void vcpu_resume(unsigned int cpu_index)
    {
        if (m_shutdown.load(std::memory_order_acquire)) return;

        std::lock_guard<std::mutex> lock(m_mcips_mutex);
        SCP_DEBUG(()) << "vcpu_resume callback for cpu_" << cpu_index;
        vCPUTime* vcpu = get_vcpu(cpu_index);

        switch (vcpu->cpu_execution_status) {
        case IDLE: {
            SCP_DEBUG(()) << "cpu_" << cpu_index << " IDLE → RUNNING";
            vcpu->cpu_execution_status = RUNNING;
            vcpu->cpu_time = qemu_time_now();

            if (m_active_vcpu.load(std::memory_order_relaxed) == nullptr) {
                const sc_core::sc_time sc_now = sc_core::sc_time(sc_core::sc_time_stamp().to_seconds(),
                                                                 sc_core::SC_SEC);
                if (sc_now > m_qemu_time) {
                    m_qemu_time = sc_now;
                    sync_qemu_time_ns();
                }

                vcpu->cpu_time = m_qemu_time;
                m_active_vcpu.store(vcpu, std::memory_order_release);

                attach_sync_window();
                set_systemc_window();
                SCP_DEBUG(()) << "[idle_pump] STOP vcpu_resume: cpu_" << cpu_index << " first to resume";
            }
            break;
        }
        case PAUSED: {
            const auto* active = m_active_vcpu.load(std::memory_order_relaxed);
            const sc_core::sc_time threshold = compute_threshold();
            const sc_core::sc_time current_qemu_time = qemu_time_now(active);
            const sc_core::sc_time vcpu_time = cpu_time_now(vcpu);

            if (threshold != sc_core::SC_ZERO_TIME && ((vcpu == active && current_qemu_time <= threshold) ||
                                                       (vcpu != active && vcpu_time <= current_qemu_time))) {
                vcpu->cpu_execution_status = RUNNING;
                m_inst.plugin_api().qemu_plugin_cpu_resume(static_cast<unsigned int>(vcpu->index));
                SCP_DEBUG(()) << "vcpu_resume: cpu_" << vcpu->index << " PAUSED → RUNNING";
            } else {
                m_inst.plugin_api().qemu_plugin_cpu_request_pause(static_cast<unsigned int>(vcpu->index));
                SCP_DEBUG(()) << "vcpu_resume: cpu_" << vcpu->index << " CAN NOT BE RESUMED";
            }
            break;
        }
        case RUNNING:
            SCP_DEBUG(()) << "vcpu_resume: cpu_" << cpu_index << " already RUNNING";
            break;

        default:
            SCP_FATAL(()) << "vcpu_resume: invalid execution status for cpu_" << cpu_index;
            sc_assert(false);
            break;
        }
    }

    /**
     * @brief Returns the current simulation time in nanoseconds to QEMU's timer thread.
     *
     * This runs without holding m_mcips_mutex or BQL. When idle, it follows
     * SystemC time and schedules idle ticks so timers keep working.
     *
     * @note delta_insn is read without m_mcips_mutex but it is updated atomically
     * by QEMU, so reading it from another thread is safe. insn_per_second is set
     * once during initialization and never changes at runtime, so reading it
     * without the mutex is also safe.
     */
    int64_t get_qemu_clock(void* /*userdata*/)
    {
        if (m_shutdown.load(std::memory_order_acquire)) {
            return static_cast<int64_t>(sc_core::sc_time_stamp().to_seconds() * NSEC_IN_ONE_SEC);
        }

        /* Lock-free: read the base time from the atomic ns shadow, never from the
         * non-atomic m_qemu_time (vCPU threads mutate that under m_mcips_mutex, so
         * reading it here would be a torn read). */
        auto active = m_active_vcpu.load(std::memory_order_acquire);
        int64_t qemu_time = m_qemu_time_ns.load(std::memory_order_acquire);

        if (!active) {
            // Clamp qemu_time to SystemC time to prevent drift at startup.
            if (qemu_time > 0) {
                int64_t systemc_time_now = static_cast<int64_t>(sc_core::sc_time_stamp().to_seconds() *
                                                                NSEC_IN_ONE_SEC);
                if (systemc_time_now > qemu_time) {
                    qemu_time = systemc_time_now;
                }
            }

            if (!m_first_vcpu_initialized.load(std::memory_order_acquire)) {
                m_idle_tick.notify(m_quantum);
                SCP_DEBUG(()) << "[idle_pump] NOTIFY from get_qemu_clock (startup, no vcpu yet)";
            }
        } else {
            // Add the active CPU's in-flight delta (delta_insn / insn_per_second are race-free).
            qemu_time += static_cast<int64_t>(cpu_delta_time(active).to_seconds() * NSEC_IN_ONE_SEC);

            // active CPU drives the clock; watch for it getting stuck (iothread livelock)
            int64_t prev = m_wd_last_clock_ns.load(std::memory_order_relaxed);
            if (prev != qemu_time) {
                // A: clock advanced since last observation.
                m_wd_last_clock_ns.store(qemu_time, std::memory_order_relaxed);
                m_wd_freeze_start_ns.store(0, std::memory_order_relaxed);
            } else {
                const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           std::chrono::steady_clock::now().time_since_epoch())
                                           .count();
                int64_t start = m_wd_freeze_start_ns.load(std::memory_order_relaxed);
                if (start == 0) {
                    // B: first call to notice the freeze; start the wall-clock stopwatch.
                    m_wd_freeze_start_ns.compare_exchange_strong(start, now_ns, std::memory_order_relaxed);
                } else if (now_ns - start > kWdFreezeThresholdNs) {
                    // D: frozen for more than the threshold -> fire the watchdog.
                    m_idle_tick.notify(sc_core::SC_ZERO_TIME);
                    m_wd_freeze_start_ns.store(now_ns, std::memory_order_relaxed); // throttle re-kicks
                }
                // C: frozen but under threshold -> keep waiting (no-op).
            }
        }

        return qemu_time;
    }

    /**
     * @brief Returns a JSON string with the current state of all CPUs (for debugging).
     * Reads without m_mcips_mutex so values may be slightly out of date.
     */
    std::string get_mcips_status_json()
    {
        auto active = m_active_vcpu.load(std::memory_order_relaxed);
        std::ostringstream os;

        const uint64_t qemu_time = static_cast<uint64_t>(qemu_time_now().to_seconds() * NSEC_IN_ONE_SEC);

        os << "{" << "\"name\":\"" << name() << "\"," << "\"qemu_time\":\"" << qemu_time << " ns\","
           << "\"n_cpus\":" << m_num_vcpus << ","
           << "\"active_vcpu_index\":" << (active ? static_cast<int64_t>(active->index) : -1) << "," << "\"vcpus\":[";

        bool first = true;
        for (int i = 0; i < m_num_vcpus; i++) {
            auto* vcpu = get_vcpu(i);
            if (!vcpu) continue;

            if (!first) os << ",";
            first = false;

            const uint64_t ns = static_cast<uint64_t>(vcpu->cpu_time.to_seconds() * NSEC_IN_ONE_SEC);

            os << "{" << "\"index\":" << vcpu->index << "," << "\"insn_per_second\":\"" << vcpu->insn_per_second
               << "\"," << "\"delta_insn\":\"" << vcpu->delta_insn << "\"," << "\"cpu_time_ns\":\"" << ns << "\","
               << "\"cpu_execution_status\":" << static_cast<int>(vcpu->cpu_execution_status) << "}";
        }
        os << "]}";

        return os.str();
    }
};

#endif //_LIBQBOX_COMPONENTS_MCIPS_PLUGIN_H
