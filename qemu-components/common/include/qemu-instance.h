/*
 * This file is part of libqbox
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef LIBQBOX_QEMU_INSTANCE_H_
#define LIBQBOX_QEMU_INSTANCE_H_

#include <cassert>
#include <sstream>
#include <systemc>

#include <cci_configuration>
#include <vector>

#include <cciutils.h>
#include <report.h>
#include <libgssync.h>

#include <libqemu-cxx/libqemu-cxx.h>

#include <dmi-manager.h>
#include <exceptions.h>

#include <scp/report.h>

#include "ports/qemu-target-signal-socket.h"
#include "mcips-plugin.h"

class QemuDeviceBaseIF
{
public:
    virtual bool can_run() { return true; }
    SCP_LOGGER();
};

/**
 * @class QemuInstanceManager
 *
 * @brief QEMU instance manager class
 *
 * @details This class manages QEMU instances. It allows to create instances
 * using the same library loader, thus allowing multiple instances of the same
 * library being loaded.
 */
class QemuInstanceManager : public sc_core::sc_module
{
public:
    using Target = qemu::Target;
    using LibLoader = qemu::LibraryLoaderIface;

protected:
    LibLoader& m_loader = qemu::get_default_lib_loader();

public:
    /**
     * @brief Construct a QemuInstanceManager. The manager will use the default
     * library loader provided by libqemu-cxx.
     */
    QemuInstanceManager(const sc_core::sc_module_name& n = "QemuInstanceManager"): sc_core::sc_module(n) {}

    /**
     * @brief Construct a QemuInstanceManager by providing a custom library loader
     *
     * @param[in] loader The custom loader
     */
    QemuInstanceManager(const sc_core::sc_module_name& n, LibLoader& loader): sc_core::sc_module(n), m_loader(loader) {}

    LibLoader& get_loader() { return m_loader; }

    /* Destructor should only be called at the end of the program, if it is called before, then all
     * Qemu instances that it manages will, of course, be destroyed too
     */
    virtual ~QemuInstanceManager() {}
};

/**
 * @class QemuInstance
 *
 * @brief This class encapsulates a libqemu-cxx qemu::LibQemu instance. It
 * handles QEMU parameters and instance initialization.
 */
class QemuInstance : public sc_core::sc_module
{
private:
    std::shared_ptr<gs::tlm_quantumkeeper_extended> m_first_qk = NULL;
    std::mutex m_lock;
    std::list<QemuDeviceBaseIF*> devices;
    cci::cci_broker_handle m_conf_broker;

    bool m_running = false;
    SCP_LOGGER();

public:
    TargetSignalSocket<bool> reset;

    // these will be used by wait_for_work when it needs a global lock
    std::mutex g_signaled_lock;
    std::condition_variable g_signaled_cond;
    bool g_signaled = false;
    std::recursive_mutex g_rec_qemu_io_lock;

    void add_dev(QemuDeviceBaseIF* d)
    {
        std::lock_guard<std::mutex> lock(m_lock);
        devices.push_back(d);
    }
    void del_dev(QemuDeviceBaseIF* d)
    {
        if (m_running) {
            std::lock_guard<std::mutex> lock(m_lock);
            devices.remove(d);
        }
    }
    bool can_run()
    {
        if (!m_running) return false;
        std::lock_guard<std::mutex> lock(m_lock);
        bool can_run = false;
        // In SINGLE mode, check if another CPU could run
        for (auto const& i : devices) {
            if (i->can_run()) {
                can_run = true;
                break;
            }
        }
        return can_run;
    }
    using Target = qemu::Target;
    using LibLoader = qemu::LibraryLoaderIface;

    enum TcgMode {
        TCG_UNSPECIFIED,
        TCG_SINGLE,
        TCG_COROUTINE,
        TCG_MULTI,
    };
    TcgMode StringToTcgMode(std::string s)
    {
        if (s == "SINGLE") return TCG_SINGLE;
        if (s == "COROUTINE") return TCG_COROUTINE;
        if (s == "MULTI") return TCG_MULTI;
        SCP_WARN(()) << "Unknown TCG mode " << s;
        return TCG_UNSPECIFIED;
    }

    enum TimeSyncStrategy {
        TIME_SYNC_QUANTUM_KEEPER,
        TIME_SYNC_MCIPS,
    };
    TimeSyncStrategy StringToTimeSyncStrategy(std::string s)
    {
        if (s == "quantum_keeper") return TIME_SYNC_QUANTUM_KEEPER;
        if (s == "mcips") return TIME_SYNC_MCIPS;
        SCP_FATAL(()) << "Unknown time_sync_strategy '" << s << "' (expected \"quantum_keeper\" or \"mcips\")";
        sc_assert(false);
        return TIME_SYNC_QUANTUM_KEEPER;
    }

protected:
    cci::cci_param<std::string> p_time_sync_strategy;
    std::unique_ptr<McipsPlugin> m_mcips_plugin;

    qemu::LibQemu m_inst;
    QemuInstanceDmiManager m_dmi_mgr;

    cci::cci_param<std::string> p_tcg_mode;
    cci::cci_param<std::string> p_sync_policy;
    TcgMode m_tcg_mode;

    cci::cci_param<bool> p_icount;
    cci::cci_param<int> p_icount_mips;

    cci::cci_param<std::string> p_args;
    std::string m_display_argument;

    cci::cci_param<std::string> p_accel;
    cci::cci_param<std::string> p_whpx_args;

    void push_default_args()
    {
        const size_t l = strlen(name()) + 1;

        m_inst.push_qemu_arg("libqbox"); /* argv[0] */
        m_inst.push_qemu_arg({
            "-M", "none",       /* no machine */
            "-m", "0",       /* Guest memory is managed by gs_memory module */
            "-monitor", "null", /* no monitor */
            "-serial", "null",  /* no serial backend */
        });

        const char* args = "qemu_args."; /* Update documentations because it's not anymore 'args.' it's 'qemu_args.' */
        for (auto p : m_conf_broker.get_unconsumed_preset_values([&](const std::pair<std::string, cci::cci_value>& iv) {
                 return iv.first.find(std::string(name()) + "." + args) == 0;
             })) {
            if (p.second.get_string().is_string()) {
                const std::string arg_name = p.first.substr(l + strlen(args));
                const std::string arg_value = p.second.get_string();
                SCP_INFO(()) << "Added QEMU argument : " << arg_name << " " << arg_value;
                m_inst.push_qemu_arg({ arg_name.c_str(), arg_value.c_str() });
            } else {
                SCP_FATAL(()) << "The value of the argument is not a string";
            }
        }

        std::stringstream ss_args(p_args);
        std::string arg;
        while (ss_args >> arg) {
            m_inst.push_qemu_arg(arg.c_str());
        }
    }

    void push_icount_mode_args()
    {
        std::ostringstream ss;
        if (p_icount_mips > 0) {
            p_icount = true;
        }
        p_icount.lock();
        p_icount_mips.lock();
        if (!p_icount) return;
        if (m_tcg_mode == TCG_MULTI) {
            SCP_FATAL(()) << "MULTI threading can not be used with icount";
            assert(m_tcg_mode != TCG_MULTI);
        }
        m_inst.push_qemu_arg("-icount");

        ss << p_icount_mips << ",sleep=off";
        m_inst.push_qemu_arg(ss.str().c_str());
    }

    void push_tcg_mode_args()
    {
        switch (m_tcg_mode) {
        case TCG_SINGLE:
            m_inst.push_qemu_arg("tcg,thread=single");
            break;

        case TCG_COROUTINE:
            m_inst.push_qemu_arg("tcg,thread=single,coroutine=on");
            break;

        case TCG_MULTI:
            m_inst.push_qemu_arg("tcg,thread=multi");
            if (p_icount) {
                SCP_FATAL(()) << "MULTI threading can not be used with icount";
                assert(!p_icount);
            }
            break;

        default:
            assert(false);
        }
    }

    void push_accelerator_args()
    {
        m_inst.push_qemu_arg("-accel");

        auto& accel = p_accel.get_value();

        if (accel == "tcg") {
            push_tcg_mode_args();
        } else if (accel == "hvf" || accel == "kvm") {
            m_inst.push_qemu_arg(accel.c_str());
        } else if (accel == "whpx") {
            auto& whpx_args_str = p_whpx_args.get_value();
            if (whpx_args_str.empty()) {
                m_inst.push_qemu_arg("whpx");
            } else {
                std::string full_arg = "whpx," + whpx_args_str;
                m_inst.push_qemu_arg(full_arg.c_str());
            }
        } else {
            SCP_FATAL(()) << "Invalid accel property '" << accel << "'";
        }
    }

    LibLoader& get_loader(sc_core::sc_object* o)
    {
        QemuInstanceManager* inst_mgr = dynamic_cast<QemuInstanceManager*>(o);
        if (!inst_mgr) {
            SCP_FATAL(SCMOD) << "Object is not a QemuInstanceManager";
        }
        return inst_mgr->get_loader();
    }

    Target strtotarget(std::string s)
    {
        if (s == "AARCH64") {
            return QemuInstance::Target::AARCH64;
        } else if (s == "RISCV64") {
            return QemuInstance::Target::RISCV64;
        } else if (s == "RISCV32") {
            return QemuInstance::Target::RISCV32;
        } else if (s == "HEXAGON") {
            return QemuInstance::Target::HEXAGON;
        } else {
            SCP_FATAL(()) << "Unable to find QEMU target container";
        }
        __builtin_unreachable();
    }

public:
    QemuInstance(const sc_core::sc_module_name& n, sc_core::sc_object* o, std::string arch)
        : QemuInstance(n, get_loader(o), strtotarget(arch))
    {
    }
    QemuInstance(const sc_core::sc_module_name& n, sc_core::sc_object* o, Target t): QemuInstance(n, get_loader(o), t)
    {
    }

    QemuInstance(const sc_core::sc_module_name& n, LibLoader& loader, Target t)
        : sc_core::sc_module(n)
        , m_conf_broker(cci::cci_get_broker())
        , m_inst(loader, t)
        , m_dmi_mgr(m_inst)
        , reset("reset")
        , p_tcg_mode("tcg_mode", "MULTI", "The TCG mode required, SINGLE, COROUTINE or MULTI")
        , p_sync_policy("sync_policy", "multithread-quantum", "Synchronization Policy to use")
        , m_tcg_mode(StringToTcgMode(p_tcg_mode))
        , p_icount("icount", false, "Enable virtual instruction counter")
        , p_icount_mips("icount_mips_shift", 0, "The MIPS shift value for icount mode (1 insn = 2^(mips) ns)")
        , p_args("qemu_args", "", "additional space separated arguments")
        , p_accel("accel", "tcg", "Virtualization accelerator")
        , p_whpx_args("whpx_args", "", "Additional WHPX accelerator properties (e.g. gicd-base-address=0x17000000)")
        , p_time_sync_strategy("time_sync_strategy", "quantum_keeper",
                               "QEMU<->SystemC time synchronization strategy: \"quantum_keeper\" (default) uses the "
                               "traditional quantum keeper; \"mcips\" syncs time based on the number of instructions "
                               "via the multi core instructions per second plugin.")
    {
        SCP_DEBUG(()) << "Libqbox QemuInstance constructor";

        auto resetcb = std::bind(&QemuInstance::reset_cb, this, sc_unnamed::_1);
        reset.register_value_changed_cb(resetcb);

        m_running = true;
        p_tcg_mode.lock();
        push_default_args();
        if (StringToTimeSyncStrategy(p_time_sync_strategy) == TIME_SYNC_MCIPS) {
            m_mcips_plugin = std::make_unique<McipsPlugin>("mcips_plugin", m_inst);
            /* Plugin filename per platform: meson shared_module() defaults */
#if defined(_WIN32)
            m_mcips_plugin->push_plugin_args("libidlinker.dll");
#elif defined(__APPLE__)
            m_mcips_plugin->push_plugin_args("libidlinker.dylib");
#else /* Linux */
            m_mcips_plugin->push_plugin_args("libidlinker.so");
#endif
        }
    }

    QemuInstance(const QemuInstance&) = delete;
    QemuInstance(QemuInstance&&) = delete;

    virtual ~QemuInstance()
    {
        m_running = false;
        if (m_mcips_plugin) {
            m_mcips_plugin.reset();
        }
    }

    bool operator==(const QemuInstance& b) const { return this == &b; }

    bool operator!=(const QemuInstance& b) const { return this != &b; }

    /**
     * @brief Add a command line argument to the qemu instance.
     *
     * This method may only be called before the instance is initialized.
     */
    void add_arg(const char* arg) { m_inst.push_qemu_arg(arg); }

    /**
     * @brief Add a the display command line argument to the qemu instance.
     *
     * This method may only be called before the instance is initialized.
     *
     */
    void set_display_arg(const std::string& arg)
    {
        // If already set to the same value, nothing to do
        if (arg == m_display_argument) {
            return;
        }

        // If already set to a different value, it's an error
        if (!m_display_argument.empty()) {
            SCP_FATAL(()) << "Display argument has already been set to '" << m_display_argument
                          << "' and cannot be changed to '" << arg
                          << "'. This may occur when mixed display modules are created (display/vnc).";
        }

        // Set the display argument for the first time
        m_display_argument = arg;
        m_inst.push_qemu_arg("-display");
        m_inst.push_qemu_arg(m_display_argument.c_str());
    }

    /**
     * @brief Add the VNC command line argument to the qemu instance.
     *
     * This method may only be called before the instance is initialized.
     */
    void set_vnc_args(const std::string& vnc_options)
    {
            m_inst.push_qemu_arg("-vnc");
            m_inst.push_qemu_arg(vnc_options.c_str());
    }

    /**
     * @brief Get the TCG mode for this instance
     *
     * @details This method is called by CPU instances determin if to use
     * coroutines or not.
     */
    TcgMode get_tcg_mode() { return m_tcg_mode; }

    bool is_kvm_enabled() const { return p_accel.get_value() == "kvm"; }
    bool is_hvf_enabled() const { return p_accel.get_value() == "hvf"; }
    bool is_whpx_enabled() const { return p_accel.get_value() == "whpx"; }
    bool is_tcg_enabled() const { return p_accel.get_value() == "tcg"; }

    /**
     * @brief Get the TCG mode for this instance
     *
     * @details This method is called by CPU instances determin if to use
     * coroutines or not.
     */
    std::shared_ptr<gs::tlm_quantumkeeper_extended> create_quantum_keeper()
    {
        std::shared_ptr<gs::tlm_quantumkeeper_extended> qk;
        /* only multi-mode sync should have separate QK's per CPU */
        if (m_first_qk && m_tcg_mode != TCG_MULTI) {
            qk = m_first_qk;
        } else {
            qk = gs::tlm_quantumkeeper_factory(p_sync_policy);
        }
        if (!qk) SCP_FATAL(())("No quantum keeper found with name : {}", p_sync_policy.get_value());
        if (!m_first_qk) m_first_qk = qk;
        if (qk->get_thread_type() == gs::SyncPolicy::SYSTEMC_THREAD) {
            assert(m_tcg_mode == TCG_COROUTINE);
        }
        /* The p_sync_policy parameter should not be modified anymore */
        p_sync_policy.lock();
        return qk;
    }

    /**
     * @brief Initialize the QEMU instance
     *
     * @details Initialize the QEMU instance with the set TCG and icount mode.
     * If the TCG mode hasn't been set, it defaults to TCG_SINGLE.
     * If icount mode hasn't been set, it defaults to ICOUNT_OFF.
     *
     * The instance should not already be initialized when calling this method.
     */
    void init()
    {
        assert(!is_inited());

        if (m_tcg_mode == TCG_UNSPECIFIED) {
            SCP_FATAL(()) << "Unknown tcg mode : " << std::string(p_tcg_mode);
        }
        // By now, if there is a CPU, it would be loaded into QEMU, and we would have a QK
        if (m_first_qk) {
            if (m_first_qk->get_thread_type() == gs::SyncPolicy::SYSTEMC_THREAD) {
                if (p_tcg_mode.is_preset_value() && m_tcg_mode != TCG_COROUTINE) {
                    SCP_WARN(()) << "This quantum keeper can only be used with TCG_COROUTINES";
                }
                m_tcg_mode = TCG_COROUTINE;
            } else {
                if (m_tcg_mode == TCG_COROUTINE) {
                    SCP_FATAL(()) << "Please select a suitable threading mode for this quantum "
                                     "keeper, it can't be used with COROUTINES";
                }
            }
        }

        push_accelerator_args();
        push_icount_mode_args();

        if (m_display_argument.empty()) {
            m_inst.push_qemu_arg({
                "-display", "none", /* no GUI */
            });
        }

        bool trace = (SCP_LOGGER_NAME().level >= sc_core::SC_FULL);
        if (trace) {
            SCP_WARN(())("Enabling QEMU debug logging");
            m_inst.push_qemu_arg({ "-d", "in_asm,int,mmu,unimp,guest_errors" });
        }

        SCP_INFO(()) << "Initializing QEMU instance with args:";

        for (const char* arg : m_inst.get_qemu_args()) {
            SCP_INFO(()) << arg;
        }

        m_inst.init();
        m_dmi_mgr.init();
    }

    /**
     * @brief Returns true if the instance is initialized
     */
    bool is_inited() const { return m_inst.is_inited(); }

    /**
     * @brief Returns the underlying qemu::LibQemu instance
     *
     * @details Returns the underlying qemu::LibQemu instance. If the instance
     * hasn't been initialized, init is called just before returning the
     * instance.
     */
    qemu::LibQemu& get()
    {
        if (!is_inited()) {
            init();
        }

        return m_inst;
    }

    /**
     * @brief Returns the mcips_plugin.
     *
     * @details Returns the mcips_plugin for the current instance.
     */
    McipsPlugin& get_mcips_plugin()
    {
        sc_assert(m_mcips_plugin);
        return *m_mcips_plugin;
    }

    /**
     * @brief Check if multi core instructions per second plugin (mcips) is enabled.
     *
     * @details Returns true only if the CCI parameter requested it AND the plugin was actually constructed.
     */
    bool is_mcips_enabled() const noexcept { return static_cast<bool>(m_mcips_plugin); }

    /**
     * @brief Returns the locked QemuInstanceDmiManager instance
     *
     * Note: we rely on RVO here so no copy happen on return (this is enforced
     * by the fact that the LockedQemuInstanceDmiManager copy constructor is
     * deleted).
     */
    QemuInstanceDmiManager& get_dmi_manager() { return m_dmi_mgr; }

    int number_devices() { return devices.size(); }

private:
    void before_end_of_elaboration() override
    {
        if (!is_inited()) {
            init(); // dlsyms libqemu_init; plugin functions are part of the returned LibQemuExports table.
        }
    }
    void start_of_simulation(void) override { get().finish_qemu_init(); }

    void reset_cb(const bool val)
    {
        if (val == 1) {
            m_inst.system_reset();
        }
    }
};

GSC_MODULE_REGISTER(QemuInstanceManager);
GSC_MODULE_REGISTER(QemuInstance, sc_core::sc_object*, std::string);

#endif
