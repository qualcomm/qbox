/*
 * This file is part of libqbox
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_CPU_CPU_H
#define _LIBQBOX_COMPONENTS_CPU_CPU_H

#include <sstream>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <cci_configuration>

#include <libgssync.h>

#include "device.h"
#include "ports/initiator.h"
#include "tlm-extensions/qemu-cpu-hint.h"
#include "ports/qemu-target-signal-socket.h"

class QemuCpu;

/*
 * QEMU<->SystemC time synchronization strategy.
 *
 * QemuCpu owns one strategy, chosen at construction, and calls a hook at each
 * point where time synchronization is involved (lifecycle, halt/reset, CPU
 * start, TLM local-time). The quantum-keeper machinery itself (deadline timer,
 * kick/end-of-loop callbacks, the CPU run loop, MTTCG signalling) lives on
 * QemuCpu; a hook either drives it or does nothing.
 *
 *  - QuantumKeeperSync : forwards every hook to the matching QemuCpu method
 *    (quantum-keeper time synchronization).
 *
 *  - McipsSync : time is driven by the MCIPS plugin, so almost every hook is a
 *    no-op. It overrides only on_end_of_elaboration() to register the CPU's
 *    instructions-per-second with the plugin. The hooks it does NOT override
 *    document precisely how much of the CPU lifecycle MCIPS participates in.
 *
 * Strategies hold no state of their own; they reach QemuCpu through m_qemu_cpu
 * (a back-reference). The two concrete strategies are nested classes of
 * QemuCpu, which grants them access to its private/protected members.
 */
class CpuTimeSyncStrategy
{
public:
    explicit CpuTimeSyncStrategy(QemuCpu& qemu_cpu): m_qemu_cpu(qemu_cpu) {}
    virtual ~CpuTimeSyncStrategy() = default;

    /* Lifecycle hooks, called from the matching QemuCpu method. */
    virtual void on_construct() {}
    /* before_end_of_elaboration: after_cpu_created runs right after m_cpu is
     * built (before socket.init); the main hook runs after set_soft_stopped. */
    virtual void on_after_cpu_created() {}
    virtual void on_before_end_of_elaboration() {}
    virtual void on_end_of_elaboration() {}
    virtual void on_end_of_simulation() {}
    virtual void on_destroy() {}

    /* halt_cb: pre runs before the QEMU halt, post after it. */
    virtual void on_halt_pre(bool val) {}
    virtual void on_halt_post() {}

    /* reset_cb: finish runs at end-of-reset, notify at the very end. */
    virtual void on_reset_finish() {}
    virtual void on_kick_notify() {}

    /* start_of_simulation: (idempotent) quantum-keeper start + deadline arm. */
    virtual void on_qk_start() {}
    virtual void on_arm_deadline() {}

    /* TLM initiator local-time hooks. */
    virtual sc_core::sc_time get_local_time(int64_t vclock_now, sc_core::sc_time sc_t) { return sc_core::SC_ZERO_TIME; }
    virtual void set_local_time(const sc_core::sc_time& t) {}

protected:
    QemuCpu& m_qemu_cpu;
    SCP_LOGGER();
};

class QemuCpu : public QemuDevice, public QemuInitiatorIface
{
    /*
     * The concrete time-sync strategies are nested classes so they can reach
     * QemuCpu's private/protected members directly.
     */

    /*
     * Traditional quantum-keeper time synchronization. Every hook forwards to
     * the matching QemuCpu method.
     */
    class QuantumKeeperSync : public CpuTimeSyncStrategy
    {
    public:
        using CpuTimeSyncStrategy::CpuTimeSyncStrategy;

        void on_construct() override;
        void on_after_cpu_created() override;
        void on_before_end_of_elaboration() override;
        void on_end_of_simulation() override;
        void on_destroy() override;
        void on_halt_pre(bool val) override;
        void on_halt_post() override;
        void on_reset_finish() override;
        void on_kick_notify() override;
        void on_qk_start() override;
        void on_arm_deadline() override;
        sc_core::sc_time get_local_time(int64_t vclock_now, sc_core::sc_time sc_t) override;
        void set_local_time(const sc_core::sc_time& t) override;
    };

    /*
     * MCIPS time synchronization. Time is driven by the MCIPS plugin, so the
     * CPU does not run a quantum keeper at all. The only thing it contributes
     * to the CPU lifecycle is registering its instructions-per-second rate with
     * the plugin at end of elaboration.
     */
    class McipsSync : public CpuTimeSyncStrategy
    {
    public:
        using CpuTimeSyncStrategy::CpuTimeSyncStrategy;

        void on_end_of_elaboration() override;
    };

private:
    inline bool mcips_enabled() const { return m_inst.is_mcips_enabled(); } // mcips: multi core instructions per second

protected:
    /*
     * We have a unique copy per CPU of this extension, which is not dynamically allocated.
     * We really don't want the default implementation to call delete on it...
     */
    class QemuCpuHintTlmExtension : public ::QemuCpuHintTlmExtension
    {
    public:
        void free() override { /* leave my extension alone, TLM */ }
    };

    gs::runonsysc m_on_sysc;
    std::shared_ptr<qemu::Timer> m_deadline_timer;
    bool m_coroutines;

    qemu::Cpu m_cpu;

    gs::async_event m_qemu_kick_ev;
    sc_core::sc_event_or_list m_external_ev;
    sc_core::sc_process_handle m_sc_thread; // used for co-routines

    std::atomic<bool> m_signaled;
    std::mutex m_signaled_lock;
    std::condition_variable m_signaled_cond;

    std::shared_ptr<gs::tlm_quantumkeeper_extended> m_qk;
    std::atomic<bool> m_finished = false;
    std::atomic<bool> m_started = false;
    enum { none, start_reset, hold_reset, finish_reset } m_resetting = none;
    gs::async_event m_start_reset_done_ev;

    std::mutex m_can_delete;
    QemuCpuHintTlmExtension m_cpu_hint_ext;
    cci::cci_param<uint64_t> m_insn_per_second;

    uint64_t m_quantum_ns; // For convenience

    /* Time synchronization strategy (quantum keeper or MCIPS), chosen at construction. */
    std::unique_ptr<CpuTimeSyncStrategy> m_time_sync;

    /*
     * Outstanding async work tracking.
     *
     * When a job is queued via async_run or async_safe_run we increment this
     * counter.  The wrapper decrements it (and signals the condvar) once the
     * job has finished executing.  The destructor waits for the counter to
     * reach zero before tearing down the object so that any in-flight job
     * cannot call back into a destroyed member (e.g. m_start_reset_done_ev).
     *
     * A timeout is used as a safety valve: if the simulation is exiting
     * because the async job itself faulted and will never complete, we do not
     * want to hang forever.
     */
    std::atomic<int> m_async_work_outstanding{ 0 };
    std::mutex m_async_work_mutex;
    std::condition_variable m_async_work_cv;
    static constexpr int ASYNC_WORK_TIMEOUT_MS = 500;

    /*
     * Wrap @job so that m_async_work_outstanding is incremented before the
     * job is queued and decremented (with a condvar notification) after it
     * returns.  Must only be called when !m_finished so that the counter
     * cannot be incremented after the destructor has started waiting.
     */
    qemu::Cpu::AsyncJobFn make_tracked_async_job(qemu::Cpu::AsyncJobFn job)
    {
        {
            std::lock_guard<std::mutex> lock(m_async_work_mutex);
            m_async_work_outstanding++;
        }
        if (m_finished) return {}; // already shutting down
        return [this, job = std::move(job)]() mutable {
            /*
             * If the CPU is already shutting down (m_finished set by
             * end_of_simulation()), skip the job body entirely.  The job
             * will never be able to complete safely anyway (the CPU is being
             * halted/unplugged), and skipping lets the destructor's wait_for
             * exit immediately rather than burning the full timeout per CPU.
             */
            if (!m_finished) {
                job();
            }
            {
                std::lock_guard<std::mutex> lock(m_async_work_mutex);
                m_async_work_outstanding--;
            }
            m_async_work_cv.notify_all();
        };
    }

    /*
     * Request quantum keeper from instance
     */
    void create_quantum_keeper()
    {
        m_qk = m_inst.create_quantum_keeper();

        if (!m_qk) {
            SCP_FATAL(()) << "qbox : Sync policy unknown";
        }

        m_qk->reset();
    }

    /*
     * Given the quantum keeper nature (synchronous or asynchronous) and the
     * p_icount parameter, we can configure the QEMU instance accordingly.
     */
    void set_coroutine_mode()
    {
        switch (m_qk->get_thread_type()) {
        case gs::SyncPolicy::SYSTEMC_THREAD:
            m_coroutines = true;
            break;

        case gs::SyncPolicy::OS_THREAD:
            m_coroutines = false;
            break;
        }
    }

    /*
     * ---- CPU loop related methods ----
     */

    /*
     * Called by watch_external_ev and kick_cb in MTTCG mode. This keeps track
     * of an external event in case the CPU thread just released the iothread
     * and is going to call wait_for_work. This is needed to avoid missing an
     * event and going to sleep while we should effectively wake-up.
     *
     * The coroutine mode does not use this method and use the SystemC kernel
     * as a mean of synchronization. If an asynchronous event is triggered
     * while the CPU thread go to sleep, the fact that the CPU thread is also
     * the SystemC thread will ensure correct ordering of the events.
     */
    void set_signaled()
    {
        assert(!m_coroutines);
        if (m_inst.get_tcg_mode() != QemuInstance::TCG_SINGLE) {
            std::lock_guard<std::mutex> lock(m_signaled_lock);
            m_signaled = true;
            m_signaled_cond.notify_all();
        } else {
            std::lock_guard<std::mutex> lock(m_inst.g_signaled_lock);
            m_inst.g_signaled = true;
            m_inst.g_signaled_cond.notify_all();
        }
    }

    /*
     * SystemC thread watching the m_external_ev event list. Only used in MTTCG
     * mode.
     */
    void watch_external_ev()
    {
        for (;;) {
            wait(m_external_ev);
            set_signaled();
        }
    }

    /*
     * Called when the CPU is kicked. We notify the corresponding async event
     * to wake the CPU up if it was sleeping waiting for work.
     */
    void kick_cb()
    {
        SCP_TRACE(())("QEMU deadline KICK callback");
        if (m_coroutines) {
            if (!m_finished) m_qemu_kick_ev.async_notify();
        } else {
            set_signaled();
        }
    }

    /*
     * Called by the QEMU iothread when the deadline timer expires. We kick the
     * CPU out of its execution loop for it to call the end_of_loop_cb callback.
     * However, we should also handle the case that qemu is currently in 'sync'
     *  - by setting the time here, we will nudge the sync thread.
     */
    void deadline_timer_cb()
    {
        SCP_TRACE(())("QEMU deadline timer callback");
        // All syncing will be done in end_of_loop_cb
        m_cpu.kick();
        // Rearm timer for next time ....
        if (!m_finished) {
            rearm_deadline_timer();

            /* Take this opportunity to set the time */
            int64_t now = m_inst.get().get_virtual_clock();
            sc_core::sc_time sc_t = sc_core::sc_time_stamp();
            if (sc_core::sc_time(now, sc_core::SC_NS) > sc_t) {
                m_qk->set(sc_core::sc_time(now, sc_core::SC_NS) - sc_t);
            }
        }
    }

    /*
     * The CPU does not have work anymore. Pause the CPU thread until we have
     * some work to do.
     *
     * - In coroutine mode, this method runs a wait on the SystemC kernel,
     *   waiting for the m_external_ev list.
     * - In MTTCG mode, we wait on the m_signaled_cond condition, signaled when
     *   set_signaled is called.
     */
    void wait_for_work()
    {
        SCP_TRACE(())("Wait for work");
        m_qk->stop();
        if (m_finished) return;

        if (m_coroutines) {
            m_on_sysc.run_on_sysc([this]() { wait(m_external_ev); });
        } else {
            if (m_inst.get_tcg_mode() != QemuInstance::TCG_SINGLE) {
                std::unique_lock<std::mutex> lock(m_signaled_lock);
                m_signaled_cond.wait(lock, [this] { return m_signaled || m_finished; });
                m_signaled = false;
            } else {
                std::unique_lock<std::mutex> lock(m_inst.g_signaled_lock);
                m_inst.g_signaled_cond.wait(lock, [this] { return m_inst.g_signaled || m_finished; });
                m_inst.g_signaled = false;
            }
        }
        if (m_finished) return;
        SCP_TRACE(())("Have work, running CPU");
        m_qk->start();
    }

    /*
     * Set the deadline timer to trigger at the end of the time budget
     */
    void rearm_deadline_timer()
    {
        // This is a simple "every quantum" tick. Whether the QK makes use of it or not
        // is down to the sync policy
        m_deadline_timer->mod(m_inst.get().get_virtual_clock() + m_quantum_ns);
    }

    /*
     * Called before running the CPU. Lock the BQL and set the deadline timer
     * to not run beyond the time budget.
     */
    void prepare_run_cpu()
    {
        /*
         * The QEMU CPU loop expect us to enter it with the iothread mutex locked.
         * It is then unlocked when we come back from the CPU loop, in
         * sync_with_kernel().
         */

        SCP_TRACE(())("Prepare run");
        if (m_inst.get_tcg_mode() == QemuInstance::TCG_SINGLE) {
            while (!m_inst.can_run() && !m_finished) {
                wait_for_work();
            }
        } else {
            while (!m_cpu.can_run() && !m_finished) {
                if (!m_coroutines && !m_inst.is_tcg_enabled()) {
                    // For hardware accelerators (KVM/HVF), break back into the
                    // QEMU loop so the vCPU thread can handle signals delivered
                    // via ioctl. TCG does not need this.
                    SCP_TRACE(())("Stopping QK (accelerator)");
                    m_qk->stop();
                    break;
                }
                wait_for_work();
            }
        }

        if (m_started) {
            m_cpu.set_soft_stopped(false);
        }
        /*
         * The QEMU CPU loop expect us to enter it with the iothread mutex locked.
         * It is then unlocked when we come back from the CPU loop, in
         * sync_with_kernel().
         */
        m_inst.get().lock_iothread();
    }

    /*
     * Run the CPU loop. Only used in coroutine mode.
     */
    void run_cpu_loop()
    {
        auto last_vclock = m_inst.get().get_virtual_clock();
        m_cpu.loop();
        /*
         * Workaround in icount mode: sometimes, the CPU does not execute
         * on the first call of run_loop(). Give it a second chance.
         */
        for (int i = 0; i < m_inst.number_devices(); i++) {
            if ((m_inst.get().get_virtual_clock() == last_vclock) && (m_cpu.can_run())) {
                m_cpu.loop();
            } else
                break;
        }
    }

    /*
     * Called after a CPU loop run. It synchronizes with the kernel.
     */
    void sync_with_kernel()
    {
        int64_t now = m_inst.get().get_virtual_clock();

        m_cpu.set_soft_stopped(true);

        m_inst.get().unlock_iothread();
        if (m_finished) return;
        if (!m_coroutines) {
            m_qk->start(); // we may have switched the QK off, so switch it on before setting
        }
        sc_core::sc_time sc_t = sc_core::sc_time_stamp();
        if (sc_core::sc_time(now, sc_core::SC_NS) > sc_t) {
            m_qk->set(sc_core::sc_time(now, sc_core::SC_NS) - sc_t);
        }
        // Important to allow QK to notify itself if it's waiting.
        m_qk->sync();
    }

    /*
     * Callback called when the CPU exits its execution loop. In coroutine
     * mode, we yield here to come back to run_cpu_loop(). In TCG thread mode,
     * we use this hook to synchronize with the kernel.
     */
    void end_of_loop_cb()
    {
        SCP_TRACE(())("End of loop");
        if (m_finished) return;
        /*
         * In MTTCG mode, vCPU threads are created during elaboration and can
         * call this callback before start_of_simulation() has completed.
         * Skip sync_with_kernel/prepare_run_cpu until fully initialized.
         */
        if (!m_started) return;

        if (m_coroutines) {
            m_inst.get().coroutine_yield();
        } else {
            std::lock_guard<std::mutex> lock(m_can_delete);
            sync_with_kernel();
            prepare_run_cpu();
        }
    }

    /*
     * SystemC thread entry when running in coroutine mode.
     */
    void mainloop_thread_coroutine()
    {
        m_cpu.register_thread();

        for (; !m_finished;) {
            prepare_run_cpu();
            run_cpu_loop();
            sync_with_kernel();
        }
    }

public:
    cci::cci_param<unsigned int> p_gdb_port;

    /* The default memory socket. Mapped to the default CPU address space in QEMU */
    QemuInitiatorSocket<> socket;
    TargetSignalSocket<bool> halt;
    TargetSignalSocket<bool> reset;

    QemuCpu(const sc_core::sc_module_name& name, QemuInstance& inst, const std::string& type_name)
        : QemuDevice(name, inst, (type_name + "-cpu").c_str())
        , halt("halt")
        , reset("reset")
        , m_qemu_kick_ev(false)
        , m_signaled(false)
        , p_gdb_port("gdb_port", 0, "Wait for gdb connection on TCP port <gdb_port>")
        , socket("mem", *this, inst)
        , m_insn_per_second("insn_per_second", 1'000'000'000, "number of instructions per second in mcips mode")
        , m_coroutines(false)
    {
        using namespace std::placeholders;

        if (mcips_enabled()) {
            m_time_sync = std::make_unique<McipsSync>(*this);
        } else {
            m_time_sync = std::make_unique<QuantumKeeperSync>(*this);
        }

        auto haltcb = std::bind(&QemuCpu::halt_cb, this, _1);
        halt.register_value_changed_cb(haltcb);
        auto resetcb = std::bind(&QemuCpu::reset_cb, this, _1);
        reset.register_value_changed_cb(resetcb);

        m_time_sync->on_construct();
        m_inst.add_dev(this);

        m_start_reset_done_ev.async_detach_suspending();
    }

    virtual ~QemuCpu()
    {
        end_of_simulation(); // catch the case we exited abnormally

        /*
         * Wait for any jobs that were already queued via async_run /
         * async_safe_run to finish executing before we destroy the object.
         * Those jobs might hold captured references.
         */
        {
            std::unique_lock<std::mutex> lock(m_async_work_mutex);
            if (!m_async_work_cv.wait_for(lock, std::chrono::milliseconds(ASYNC_WORK_TIMEOUT_MS),
                                          [this] { return m_async_work_outstanding == 0; })) {
                SCP_WARN(()) << "Timeout waiting for " << m_async_work_outstanding.load()
                             << " outstanding async work(s) to complete during destruction";
                // We may arrive here if the QEMU thread never actually started, there was queue'd work waiting for it,
                // but the simulation has been terminated.
            }
        }

        m_time_sync->on_destroy();
        m_inst.del_dev(this);
    }

    // Process shutting down the CPU's at end of simulation, check this was done on destruction.
    // This gives time for QEMU to exit etc.
    void end_of_simulation() override
    {
        if (m_finished) return;
        m_finished = true; // assert before taking lock (for co-routines too)

        if (!m_cpu.valid()) {
            /* CPU hasn't been created yet */
            return;
        }

        if (!m_realized) {
            return;
        }

        /*
         * If start_of_simulation() was never called (e.g. the simulation
         * aborted during elaboration) then finish_qemu_init() was never
         * called either, and the QEMU iothread is still in its startup
         * wait.  Calling lock_iothread() in that state blocks forever
         * inside wait_for_iothread_startup.  Nothing useful can be done
         * without a running iothread, so bail out early.
         */
        if (!m_started) {
            return;
        }

        m_inst.get().lock_iothread();
        /* Make sure QEMU won't call us anymore */
        m_cpu.clear_callbacks();

        if (m_coroutines) {
            // can't join or wait for sc_event
            m_inst.get().unlock_iothread();
            return;
        }

        /* Unblock it if it's waiting for run budget, and unblock the CPU thread if it's sleeping */
        m_time_sync->on_end_of_simulation();

        /* Wait for QEMU to terminate the CPU thread */
        /*
         * Theoretically we should m_cpu.remove_sync(); here, however if QEMU is in the process of an io operation or an
         * exclusive cpu region, it will end up waiting for the io operation to finish (effectively waiting for the
         * SystemC thread, or potentially another CPU that wont get the chance to exit)
         */
        m_cpu.halt(true);

        m_inst.get().unlock_iothread();
        m_cpu.kick(); // Just in case the CPU is currently in the big lock waiting
        m_cpu.set_unplug(true);
    }

    /* NB this is usd to determin if this cpu can run in SINGLE mode
     * for the m_inst.can_run calculation
     */
    bool can_run() override { return m_cpu.can_run(); }

    void before_end_of_elaboration() override
    {
        QemuDevice::before_end_of_elaboration();

        m_cpu = qemu::Cpu(m_dev);

        m_time_sync->on_after_cpu_created();

        socket.init(m_dev, "memory");

        m_cpu.set_soft_stopped(true);

        m_time_sync->on_before_end_of_elaboration();

        m_cpu_hint_ext.set_cpu(m_cpu);
    }

    void halt_cb(const bool& val)
    {
        SCP_TRACE(())("Halt : {}", val);
        if (!m_finished) {
            m_time_sync->on_halt_pre(val);
            m_inst.get().lock_iothread();
            m_cpu.halt(val);
            m_inst.get().unlock_iothread();
            m_time_sync->on_halt_post();
        }
    }

    /* NB _MUST_ be called from an SC_THREAD */
    void reset_cb(const bool& val)
    {
        /* Assume this is on the SystemC thread, so no race condition issues */
        if (m_finished) return;

        if (val) {
            if (m_resetting != none) return; // dont double reset!
            SCP_WARN(())("Start reset");
            m_resetting = start_reset;
            m_cpu.async_safe_run(make_tracked_async_job([this] {
                m_cpu.reset(true);
                m_resetting = hold_reset;
                m_start_reset_done_ev.async_notify();
            })); // start the reset (which will pause the CPU)
        } else {
            if (m_resetting == none) return; // dont finish a finished reset!
            while (m_resetting == start_reset) {
                SCP_WARN(())("Hold reset");
                m_cpu.kick(); // without this kick, async_safe_run may not be called (QEMU race)
                sc_core::wait(m_start_reset_done_ev);
            }
            m_inst.get().lock_iothread();
            socket.reset(); // remove DMI's (needs BQL for memory region updates)
            m_inst.get().unlock_iothread();
            m_cpu.reset(false); // call the end-of-reset (which will unpause the CPU)
            m_time_sync->on_reset_finish();
            SCP_WARN(())("Finished reset");
            m_resetting = none;
        }
        m_time_sync->on_kick_notify();
    }
    virtual void end_of_elaboration() override
    {
        QemuDevice::end_of_elaboration();
        m_time_sync->on_end_of_elaboration();
        if (!p_gdb_port.is_default_value()) {
            std::stringstream ss;
            SCP_INFO(()) << "Starting gdb server on TCP port " << p_gdb_port;
            ss << "tcp::" << p_gdb_port;
            m_inst.get().start_gdb_server(ss.str());
        }
    }

    virtual void start_of_simulation() override
    {
        m_quantum_ns = int64_t(tlm_utils::tlm_quantumkeeper::get_global_quantum().to_seconds() * 1e9);

        QemuDevice::start_of_simulation();
        if (m_inst.get_tcg_mode() == QemuInstance::TCG_SINGLE) {
            if (m_inst.can_run()) {
                m_time_sync->on_qk_start();
            }
        } else if (!m_coroutines) {
            /*
             * In MTTCG mode, start the QK to register a suspending channel
             * with the SystemC kernel. Without this, async_suspend() returns
             * true (exit) whenever there are no pending events, which can
             * happen in the gap between MMIO transactions processed by
             * run_on_sysc(). The QK will be stopped later in wait_for_work()
             * when the CPU halts (e.g. WFI), allowing normal starvation exit.
             */
            m_time_sync->on_qk_start();
        }

        m_started = true;
        if (!m_coroutines) {
            /*
             * Start the quantum keeper before kicking the CPU to ensure
             * its tick event is attached as suspending. Without this, a
             * fast CPU could complete and stop its QK before a slow CPU
             * ever calls sync_with_kernel() (where start() was previously
             * first called), leaving no suspending events and causing
             * premature simulation exit due to starvation.
             */
            m_time_sync->on_qk_start();

            /* Prepare the CPU for its first run and release it
             * Hold BQL to synchronize with the vCPU thread's idle-wait loop
             * in qemu_process_cpu_events(). That loop checks cpu_thread_is_idle()
             * (which reads soft_stopped) under BQL, then enters
             * qemu_cond_wait(halt_cond, &bql) which atomically releases BQL.
             * Without BQL here, the kick (broadcast on halt_cond) can be lost
             * if the vCPU thread is between the idle check and the cond_wait.
             */
            m_inst.get().lock_iothread();
            m_cpu.set_soft_stopped(false);
            m_time_sync->on_arm_deadline();
            m_cpu.kick();
            m_inst.get().unlock_iothread();
        }

        // Have not managed to figure out the root cause of the issue, but the
        // PC is not properly set before running KVM, or it is possibly reset to
        // 0 by some routine. By setting the vcpu as dirty, we trigger pushing
        // registers to KVM just before running it.
        m_cpu.set_vcpu_dirty(true);
    }

    /* QemuInitiatorIface  */
    virtual void initiator_customize_tlm_payload(TlmPayload& payload) override
    {
        /* Signal the other end we are a CPU */
        payload.set_extension(&m_cpu_hint_ext);
    }

    virtual void initiator_tidy_tlm_payload(TlmPayload& payload) override { payload.clear_extension(&m_cpu_hint_ext); }

    /*
     * Called by the initiator socket just before a memory transaction.
     * We update our current view of the local time and return it.
     */
    virtual sc_core::sc_time initiator_get_local_time() override
    {
        if (m_finished) return sc_core::SC_ZERO_TIME;

        int64_t vclock_now = m_inst.get().get_virtual_clock();
        sc_core::sc_time sc_t = sc_core::sc_time_stamp();
        return m_time_sync->get_local_time(vclock_now, sc_t);
    }

    /*
     * Called after the transaction. We must update our local time view to
     * match t.
     */
    virtual void initiator_set_local_time(const sc_core::sc_time& t) override
    {
        if (m_finished) return;
        m_time_sync->set_local_time(t);
    }

    /* expose async run interface for DMI invalidation */
    virtual void initiator_async_run(qemu::Cpu::AsyncJobFn job) override
    {
        if (!m_finished) m_cpu.async_run(make_tracked_async_job(std::move(job)));
    }
};

/*
 * ---- QuantumKeeperSync out-of-line definitions ----
 *
 * Defined after QemuCpu so they can reach its members and methods (as a nested
 * class) through m_qemu_cpu. Each hook simply forwards to the matching
 * QemuCpu method.
 */

inline void QemuCpu::QuantumKeeperSync::on_construct()
{
    m_qemu_cpu.m_external_ev |= m_qemu_cpu.m_qemu_kick_ev;

    m_qemu_cpu.create_quantum_keeper();
    m_qemu_cpu.set_coroutine_mode();
    if (!m_qemu_cpu.m_coroutines) {
        sc_core::sc_spawn(std::bind(&QemuCpu::watch_external_ev, &m_qemu_cpu), "watch_external_ev");
    }
}

inline void QemuCpu::QuantumKeeperSync::on_after_cpu_created()
{
    if (m_qemu_cpu.m_coroutines) {
        m_qemu_cpu.m_sc_thread = sc_core::sc_spawn(std::bind(&QemuCpu::mainloop_thread_coroutine, &m_qemu_cpu));
    }
}

inline void QemuCpu::QuantumKeeperSync::on_before_end_of_elaboration()
{
    m_qemu_cpu.m_cpu.set_end_of_loop_callback(std::bind(&QemuCpu::end_of_loop_cb, &m_qemu_cpu));
    m_qemu_cpu.m_cpu.set_kick_callback(std::bind(&QemuCpu::kick_cb, &m_qemu_cpu));
    m_qemu_cpu.m_deadline_timer = m_qemu_cpu.m_inst.get().timer_new();
    m_qemu_cpu.m_deadline_timer->set_callback(std::bind(&QemuCpu::deadline_timer_cb, &m_qemu_cpu));
}

inline void QemuCpu::QuantumKeeperSync::on_end_of_simulation()
{
    m_qemu_cpu.m_qk->stop();
    /* Unblock the CPU thread if it's sleeping */
    m_qemu_cpu.set_signaled();
}

inline void QemuCpu::QuantumKeeperSync::on_destroy()
{
    while (!m_qemu_cpu.m_can_delete.try_lock()) {
        m_qemu_cpu.m_qk->stop();
    }
}

inline void QemuCpu::QuantumKeeperSync::on_halt_pre(bool val)
{
    if (val) {
        m_qemu_cpu.m_deadline_timer->del();
        m_qemu_cpu.m_qk->stop();
    } else {
        m_qemu_cpu.m_qk->start();
        m_qemu_cpu.rearm_deadline_timer();
    }
}

inline void QemuCpu::QuantumKeeperSync::on_halt_post()
{
    m_qemu_cpu.m_qemu_kick_ev.async_notify(); // notify the other thread so that the CPU is allowed to continue
}

inline void QemuCpu::QuantumKeeperSync::on_reset_finish()
{
    m_qemu_cpu.m_qk->start(); // restart the QK if it's stopped
    m_qemu_cpu.m_qk->reset();
    m_qemu_cpu.m_qemu_kick_ev.async_notify(); // notify the other thread so that the CPU is allowed to continue
}

inline void QemuCpu::QuantumKeeperSync::on_kick_notify()
{
    m_qemu_cpu.m_qemu_kick_ev
        .async_notify(); // notify the other thread so that the CPU is allowed to process if required
}

inline void QemuCpu::QuantumKeeperSync::on_qk_start() { m_qemu_cpu.m_qk->start(); }

inline void QemuCpu::QuantumKeeperSync::on_arm_deadline() { m_qemu_cpu.rearm_deadline_timer(); }

inline sc_core::sc_time QemuCpu::QuantumKeeperSync::get_local_time(int64_t vclock_now, sc_core::sc_time sc_t)
{
    using sc_core::sc_time;
    using sc_core::SC_NS;

    if (sc_time(vclock_now, SC_NS) > sc_t) {
        m_qemu_cpu.m_qk->set(sc_time(vclock_now, SC_NS) - sc_t);
        return m_qemu_cpu.m_qk->get_local_time();
    }
    return sc_core::SC_ZERO_TIME;
}

inline void QemuCpu::QuantumKeeperSync::set_local_time(const sc_core::sc_time& t)
{
    m_qemu_cpu.m_qk->set(t);

    if (m_qemu_cpu.m_qk->need_sync()) {
        /*
         * Kick the CPU out of its execution loop so that we can sync with
         * the kernel.
         */
        m_qemu_cpu.m_cpu.kick();
    }
}

/*
 * ---- McipsSync out-of-line definitions ----
 */

inline void QemuCpu::McipsSync::on_end_of_elaboration()
{
    if (!m_qemu_cpu.m_inst.get_mcips_plugin().set_vcpu_insn_per_second(m_qemu_cpu.m_cpu.get_index(),
                                                                       m_qemu_cpu.m_insn_per_second)) {
        SCP_FATAL(()) << "Failed to set insn_per_second for cpu_" << m_qemu_cpu.m_cpu.get_index();
        sc_assert(false);
    }
}

#endif
