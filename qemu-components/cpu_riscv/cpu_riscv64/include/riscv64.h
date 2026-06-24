/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <string>
#include <functional>

#include <cci_configuration>
#include <libqemu-cxx/target/riscv.h>

#include <libgssync.h>
#include <module_factory_registery.h>

#include <cpu.h>

class QemuCpuRiscv64 : public QemuCpu
{
public:
    sc_core::sc_vector<QemuTargetSignalSocket> irq_in;

    // CCI parameters for RISC-V CPU configuration
    cci::cci_param<uint64_t> p_resetvec;
    cci::cci_param<bool> p_pmp;
    cci::cci_param<bool> p_smaia;
    cci::cci_param<bool> p_ssaia;

protected:
    uint64_t m_hartid;
    gs::async_event m_irq_ev;

    void mip_update_cb(uint32_t value)
    {
        if (value) {
            m_irq_ev.notify();
        }
    }

public:
    QemuCpuRiscv64(const sc_core::sc_module_name& name, QemuInstance& inst, const char* model, uint64_t hartid)
        : QemuCpu(name, inst, std::string(model) + "-riscv")
        , m_hartid(hartid)
        /*
         * We have no choice but to attach-suspend here. This is fixable but
         * non-trivial. It means that the SystemC kernel will never starve...
         */
        , m_irq_ev(true)
        , irq_in("irq_in", 32)
        // Initialize CCI parameters with default values
        , p_resetvec("resetvec", 0x0, "Reset vector address")
        , p_pmp("pmp", false, "Enable Physical Memory Protection")
        , p_smaia("smaia", false, "Enable Smaia (AIA M-mode CSRs)")
        , p_ssaia("ssaia", false, "Enable Ssaia (AIA S-mode CSRs)")
    {
        m_external_ev |= m_irq_ev;
        for (auto& irq : irq_in) {
            m_external_ev |= irq->default_event();
        }
    }

    void before_end_of_elaboration()
    {
        QemuCpu::before_end_of_elaboration();

        qemu::CpuRiscv64 cpu(get_qemu_dev());
        cpu.set_prop_int("hartid", m_hartid);

        // Set properties from CCI parameters
        cpu.set_prop_int("resetvec", p_resetvec);
        cpu.set_prop_bool("pmp", p_pmp);
        if (p_smaia) {
            cpu.set_prop_bool("smaia", true);
        }
        if (p_ssaia) {
            cpu.set_prop_bool("ssaia", true);
        }

        cpu.set_mip_update_callback(std::bind(&QemuCpuRiscv64::mip_update_cb, this, std::placeholders::_1));
    }

    void end_of_elaboration() override
    {
        QemuCpu::end_of_elaboration();

        // Initialize IRQ sockets with GPIO pin numbers 0-31
        for (int i = 0; i < 32; i++) {
            irq_in[i].init(m_dev, i);
        }

        // Register reset handler - needed for proper reset behavior when system reset is requested
        qemu::CpuRiscv32 cpu(get_qemu_dev());
        cpu.register_reset();
    }
};

class cpu_riscv64 : public QemuCpuRiscv64
{
public:
    cpu_riscv64(const sc_core::sc_module_name& name, sc_core::sc_object* o, uint64_t hartid)
        : cpu_riscv64(name, *(dynamic_cast<QemuInstance*>(o)), hartid)
    {
    }
    cpu_riscv64(const sc_core::sc_module_name& n, QemuInstance& inst, uint64_t hartid)
        : QemuCpuRiscv64(n, inst, "rv64", hartid)
    {
    }
};

extern "C" void module_register();
