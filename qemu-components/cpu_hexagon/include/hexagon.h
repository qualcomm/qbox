/*
 * This file is part of libqbox
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <string>
#include <functional>
#include <optional>

#include <libqemu-cxx/target/hexagon.h>

#include <module_factory_registery.h>
#include <libgssync.h>

#include <cpu.h>
#include <hexagon_globalreg.h>
#include <hexagon-l2vic.h>
#include <hexagon_tlb.h>

class qemu_cpu_hexagon : public QemuCpu
{
    cci::cci_broker_handle m_broker;
    hexagon_globalreg* p_hex_greg;
    hexagon_l2vic* m_l2vic;
    hexagon_tlb* m_hex_tlb;

public:
    static constexpr qemu::Target ARCH = qemu::Target::HEXAGON;
    sc_core::sc_vector<QemuTargetSignalSocket> irq_in;

    qemu_cpu_hexagon(const sc_core::sc_module_name& name, sc_core::sc_object* o1, sc_core::sc_object* o2)
        : qemu_cpu_hexagon(name, *(dynamic_cast<QemuInstance*>(o1)), dynamic_cast<hexagon_globalreg*>(o2))
    {
    }
    qemu_cpu_hexagon(const sc_core::sc_module_name& name, sc_core::sc_object* o1, sc_core::sc_object* o2,
                     sc_core::sc_object* o3)
        : qemu_cpu_hexagon(name, *(dynamic_cast<QemuInstance*>(o1)), dynamic_cast<hexagon_globalreg*>(o2),
                           dynamic_cast<hexagon_l2vic*>(o3))
    {
    }
    qemu_cpu_hexagon(const sc_core::sc_module_name& name, sc_core::sc_object* o1, sc_core::sc_object* o2,
                     sc_core::sc_object* o3, sc_core::sc_object* o4)
        : qemu_cpu_hexagon(name, *(dynamic_cast<QemuInstance*>(o1)), dynamic_cast<hexagon_globalreg*>(o2),
                           dynamic_cast<hexagon_l2vic*>(o3), dynamic_cast<hexagon_tlb*>(o4))
    {
    }
    qemu_cpu_hexagon(const sc_core::sc_module_name& name, QemuInstance& inst, hexagon_globalreg* hex_greg = nullptr,
                     hexagon_l2vic* l2vic = nullptr, hexagon_tlb* tlb = nullptr)
        : QemuCpu(name, inst, "v67-hexagon")
        , m_broker(cci::cci_get_broker())
        , irq_in("irq_in", 8, [](const char* n, int i) { return new QemuTargetSignalSocket(n); })
        , p_cfgbase("config_table_addr", 0xffffffffULL, "config table address")
        , p_vp_mode("vp_mode", true, "override the vp_mode for testing")
        , p_semihosting("enable_semihosting", false, "enable semihosting for debugging/testing")
        , p_dsp_arch("dsp_arch", "v68", "DSP arch")
        , p_start_powered_off("start_powered_off", false,
                              "Start and reset the CPU "
                              "in powered-off state")
        , p_sched_limit("sched_limit", true, "use sched limit")
        , p_paranoid("paranoid_commit_state", false, "enable per-packet checks")
        , p_subsystem_id("subsystem_id", 0, "subsystem id")
        , p_hexagon_num_threads("hexagon_num_threads", 8,
                                "number of hexagon threads") // THREADS_MAX in ./target/hexagon/cpu.h
        , p_isdben_trusted("isdben_trusted", true, "isdben trusted")
        , p_isdben_secure("isdben_secure", true, "isdben secure")
        , p_cmdline("cmdline", "", "cmdline")
        , p_vtcm_base_addr("vtcm_base_addr", 0, "vtcm base address")
        , p_vtcm_size_kb("vtcm_size_kb", 0, "vtcm size in kb")
        , p_coproc2_present("coproc2_present", false, "secondary coprocessor is present")
        , p_hvx_contexts("hvx_contexts", 0, "number of HVX contexts")
        , p_hex_greg(hex_greg)
        , m_l2vic(l2vic)
        , m_hex_tlb(tlb)
    /*
     * We have no choice but to attach-suspend here. This is fixable but
     * non-trivial. It means that the SystemC kernel will never starve...
     */
    {
        for (int i = 0; i < irq_in.size(); ++i) {
            m_external_ev |= irq_in[i]->default_event();
        }
    }

    void set_l2vic(hexagon_l2vic* l2vic) { m_l2vic = l2vic; }
    void set_hex_tlb(hexagon_tlb* tlb) { m_hex_tlb = tlb; }

    void before_end_of_elaboration() override
    {
        const std::string dsp_arch = p_dsp_arch.get_value();
        set_qom_type(dsp_arch + "-hexagon-cpu");
        // set the parameter config-table-addr 195 hexagon_testboard
        QemuCpu::before_end_of_elaboration();
        qemu::CpuHexagon cpu(get_qemu_dev());

        qemu::CpuHexagon::Rev_t dsp_rev = qemu::CpuHexagon::parse_dsp_arch(dsp_arch);
        if (dsp_rev == qemu::CpuHexagon::unknown_rev) {
            SCP_FATAL(())("Unrecognized Architecture Revision: " + dsp_arch);
        }
        cpu.set_prop_bool("start-powered-off", p_start_powered_off);
        cpu.set_prop_bool("sched-limit", p_sched_limit);
        cpu.set_prop_bool("virtual-platform-mode", p_vp_mode);
        cpu.set_prop_bool("enable-semihosting", p_semihosting);
        cpu.set_prop_bool("paranoid-commit-state", p_paranoid);
        cpu.set_prop_int("subsystem-id", p_subsystem_id);
        cpu.set_prop_int("thread-count", p_hexagon_num_threads);
        cpu.set_prop_str("cmdline", p_cmdline.get_value().data());
        cpu.set_prop_int("vtcm-base-addr", p_vtcm_base_addr);
        cpu.set_prop_int("vtcm-size-kb", p_vtcm_size_kb);
        cpu.set_prop_bool("coproc2-present", p_coproc2_present);
        cpu.set_prop_int("hvx-contexts", p_hvx_contexts);
        if (p_hex_greg) {
            p_hex_greg->before_end_of_elaboration();
            qemu::Device hex_greg_dev = p_hex_greg->get_qemu_dev();
            qemu::Device cpu_dev = get_qemu_dev();
            cpu_dev.set_prop_link("global-regs", hex_greg_dev);
        }

        if (m_l2vic) {
            m_l2vic->before_end_of_elaboration();
            qemu::Device cpu_dev = get_qemu_dev();
            cpu_dev.set_prop_link("l2vic", m_l2vic->get_qemu_dev());
        }

        if (m_hex_tlb) {
            m_hex_tlb->before_end_of_elaboration();
            qemu::Device cpu_dev = get_qemu_dev();
            cpu_dev.set_prop_link("tlb", m_hex_tlb->get_qemu_dev());
        }
    }

    qemu::CpuHexagon get_cpu_hexagon() const { return qemu::CpuHexagon(m_cpu); }

    void end_of_elaboration() override
    {
        QemuCpu::end_of_elaboration();

        get_cpu_hexagon().register_reset();

        for (int i = 0; i < irq_in.size(); ++i) {
            irq_in[i].init(m_dev, i);
        }
    }

public:
    cci::cci_param<uint64_t> p_cfgbase;
    cci::cci_param<bool> p_vp_mode;
    cci::cci_param<bool> p_semihosting;
    cci::cci_param<bool> p_start_powered_off;
    cci::cci_param<bool> p_sched_limit;
    cci::cci_param<bool> p_paranoid;
    cci::cci_param<std::string> p_dsp_arch;
    cci::cci_param<uint32_t> p_subsystem_id;
    cci::cci_param<uint32_t> p_hexagon_num_threads;
    cci::cci_param<bool> p_isdben_trusted;
    cci::cci_param<bool> p_isdben_secure;
    cci::cci_param<std::string> p_cmdline;
    cci::cci_param<uint64_t> p_vtcm_base_addr;
    cci::cci_param<uint32_t> p_vtcm_size_kb;
    cci::cci_param<bool> p_coproc2_present;
    cci::cci_param<uint32_t> p_hvx_contexts;
};

extern "C" void module_register();
