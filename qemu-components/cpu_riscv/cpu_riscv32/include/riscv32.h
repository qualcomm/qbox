/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
#include <ports/qemu-target-signal-socket.h>
#include <ports/qemu-initiator-signal-socket.h>

class QemuCpuRiscv32 : public QemuCpu
{
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
    // IRQ input sockets - sc_vector of 32 IRQs
    sc_core::sc_vector<QemuTargetSignalSocket> irq_in;

    // CCI parameters for RISC-V CPU configuration
    cci::cci_param<uint64_t> p_hartid;
    cci::cci_param<bool> p_debug;
    cci::cci_param<bool> p_pmp;
    cci::cci_param<bool> p_mmu;
    cci::cci_param<std::string> p_priv_spec;
    cci::cci_param<uint64_t> p_cbom_blocksize;
    cci::cci_param<uint64_t> p_cbop_blocksize;
    cci::cci_param<uint64_t> p_cboz_blocksize;
    cci::cci_param<uint64_t> p_mvendorid;
    cci::cci_param<uint64_t> p_mimpid;
    cci::cci_param<uint64_t> p_marchid;
    cci::cci_param<uint64_t> p_resetvec;
    cci::cci_param<bool> p_smrnmi;
    cci::cci_param<bool> p_smaia;
    cci::cci_param<bool> p_ssaia;
    QemuCpuRiscv32(const sc_core::sc_module_name& name, QemuInstance& inst, const char* model, uint64_t hartid)
        : QemuCpu(name, inst, std::string(model) + "-riscv")
        , m_hartid(hartid)
        /*
         * We have no choice but to attach-suspend here. This is fixable but
         * non-trivial. It means that the SystemC kernel will never starve...
         */
        , m_irq_ev(true)
        // Initialize IRQ vector with 32 sockets
        , irq_in("irq_in", 32)
        // Initialize CCI parameters with default values
        , p_hartid("hartid", hartid, "Hardware thread ID")
        , p_debug("debug", true, "Enable debug support")
        , p_pmp("pmp", false, "Enable Physical Memory Protection")
        , p_mmu("mmu", true, "Enable Memory Management Unit")
        , p_priv_spec("priv_spec", "v1.12.0", "Privilege specification version")
        , p_cbom_blocksize("cbom_blocksize", 64, "Cache block management operation block size")
        , p_cbop_blocksize("cbop_blocksize", 64, "Cache block prefetch operation block size")
        , p_cboz_blocksize("cboz_blocksize", 64, "Cache block zero operation block size")
        , p_mvendorid("mvendorid", 0, "Vendor ID")
        , p_mimpid("mimpid", 0, "Implementation ID")
        , p_marchid("marchid", 0, "Architecture ID")
        , p_resetvec("resetvec", 0x0, "Reset vector address")
        , p_smrnmi("smrnmi", false, "Enable Smrnmi (RNMI M-mode CSRs 0x740-0x744)")
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

        qemu::CpuRiscv32 cpu(get_qemu_dev());
        cpu.set_prop_int("hartid", p_hartid);

        // Set properties from CCI parameters
        cpu.set_prop_int("resetvec", p_resetvec);
        cpu.set_prop_bool("debug", p_debug);
        cpu.set_prop_bool("pmp", p_pmp);
        cpu.set_prop_bool("mmu", p_mmu);
        cpu.set_prop_str("priv_spec", p_priv_spec.get_value().c_str());

        if (p_smrnmi) {
            cpu.set_prop_bool("smrnmi", true);
        }
        if (p_smaia) {
            cpu.set_prop_bool("smaia", true);
        }
        if (p_ssaia) {
            cpu.set_prop_bool("ssaia", true);
        }

        // Cache block operation sizes
        cpu.set_prop_int("cbom_blocksize", p_cbom_blocksize);
        cpu.set_prop_int("cbop_blocksize", p_cbop_blocksize);
        cpu.set_prop_int("cboz_blocksize", p_cboz_blocksize);

        // Vendor ID settings
        cpu.set_prop_int("mvendorid", p_mvendorid);
        cpu.set_prop_int("mimpid", p_mimpid);
        cpu.set_prop_int("marchid", p_marchid);

        cpu.set_mip_update_callback(std::bind(&QemuCpuRiscv32::mip_update_cb, this, std::placeholders::_1));
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

class cpu_riscv32 : public QemuCpuRiscv32
{
public:
    static constexpr qemu::Target ARCH = qemu::Target::RISCV32;

    cpu_riscv32(const sc_core::sc_module_name& name, sc_core::sc_object* o, uint64_t hartid)
        : cpu_riscv32(name, *(dynamic_cast<QemuInstance*>(o)), hartid)
    {
    }
    cpu_riscv32(const sc_core::sc_module_name& n, QemuInstance& inst, uint64_t hartid)
        : QemuCpuRiscv32(n, inst, "rv32", hartid)
    {
    }
    // Constructor for test framework compatibility (uses hartid=0 by default)
    cpu_riscv32(const sc_core::sc_module_name& n, QemuInstance& inst): QemuCpuRiscv32(n, inst, "rv32", 0) {}
};

extern "C" void module_register();
