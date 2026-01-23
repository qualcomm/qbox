/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <string>

#include <cci_configuration>

#include <module_factory_registery.h>

#include <libqemu-cxx/target/aarch64.h>

#include <arm.h>
#include <device.h>
#include <ports/qemu-initiator-signal-socket.h>
#include <ports/qemu-target-signal-socket.h>
#include <ports/target.h>

class cpu_arm_cortexR52 : public QemuCpuArm
{
protected:
    int get_psci_conduit_val() const
    {
        if (p_psci_conduit.get_value() == "disabled") {
            return 0;
        } else if (p_psci_conduit.get_value() == "smc") {
            return 1;
        } else if (p_psci_conduit.get_value() == "hvc") {
            return 2;
        } else {
            /* TODO: report warning */
            return 0;
        }
    }

public:
    cci::cci_param<unsigned int> p_mp_affinity;
    cci::cci_param<bool> p_start_powered_off;
    cci::cci_param<uint64_t> p_rvbar;
    cci::cci_param<uint64_t> p_cntfrq_hz;
    cci::cci_param<bool> p_has_el2;
    cci::cci_param<std::string> p_psci_conduit;
    cci::cci_param<uint8_t> p_num_mpu_regions;
    cci::cci_param<uint8_t> p_num_mpu_hyp_regions;
    cci::cci_param<uint64_t> p_reset_cbar;
    cci::cci_param<uint32_t> p_imp_buildoptr;

    QemuTargetSignalSocket irq_in;
    QemuTargetSignalSocket fiq_in;
    QemuTargetSignalSocket virq_in;
    QemuTargetSignalSocket vfiq_in;

    QemuInitiatorSignalSocket irq_timer_phys_out;
    QemuInitiatorSignalSocket irq_timer_virt_out;
    QemuInitiatorSignalSocket irq_timer_hyp_out;
    QemuInitiatorSignalSocket irq_timer_sec_out;

    cpu_arm_cortexR52(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : cpu_arm_cortexR52(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }
    cpu_arm_cortexR52(sc_core::sc_module_name name, QemuInstance& inst)
        : QemuCpuArm(name, inst, "cortex-r52-arm")
        , p_mp_affinity("mp_affinity", 0, "Multi-processor affinity value")
        , p_has_el2("has_el2", true, "ARM virtualization extensions")
        , p_rvbar("rvbar", 0ull, "Reset vector base address register value")
        , p_cntfrq_hz("cntfrq_hz", 0ull, "CPU Generic Timer CNTFRQ in Hz")
        , p_start_powered_off("start_powered_off", false,
                              "Start and reset the CPU "
                              "in powered-off state")
        , p_psci_conduit("psci_conduit", "disabled",
                         "Set the QEMU PSCI conduit: "
                         "disabled->no conduit, "
                         "hvc->through hvc call, "
                         "smc->through smc call")
        , p_num_mpu_regions("pmsav7_dregion", 16, "PMSAv7 MPU number of supported regions")
        , p_num_mpu_hyp_regions("pmsav8r_hdregion", 16, "PMSAv8 MPU number of supported hyp regions")
        , p_reset_cbar("reset_cbar", 0ull, "Reset Configuration Base Address Register")
        , p_imp_buildoptr("imp_buildoptr", 0ull, "IMP_BUILDOPTR Cortex R52 register value")
        , irq_in("irq_in")
        , fiq_in("fiq_in")
        , virq_in("virq_in")
        , vfiq_in("vfiq_in")
        , irq_timer_phys_out("irq_timer_phys_out")
        , irq_timer_virt_out("irq_timer_virt_out")
        , irq_timer_hyp_out("irq_timer_hyp_out")
        , irq_timer_sec_out("irq_timer_sec_out")
    {
    }

    void before_end_of_elaboration() override
    {
        QemuCpuArm::before_end_of_elaboration();

        qemu::CpuArm cpu(m_dev);

        if (!p_mp_affinity.is_default_value()) {
            cpu.set_prop_int("mp-affinity", p_mp_affinity);
        }

        cpu.set_prop_bool("has_el2", p_has_el2);

        cpu.set_prop_bool("start-powered-off", p_start_powered_off);
        cpu.set_prop_int("rvbar", p_rvbar);
        cpu.set_prop_int("psci-conduit", get_psci_conduit_val());
        cpu.set_prop_int("pmsav7-dregion", p_num_mpu_regions);
        cpu.set_prop_int("pmsav8r-hdregion", p_num_mpu_hyp_regions);
        cpu.set_prop_int("reset-cbar", p_reset_cbar);
        cpu.set_imp_buildoptr(p_imp_buildoptr);
        if (!p_cntfrq_hz.is_default_value()) {
            cpu.set_prop_int("cntfrq", p_cntfrq_hz);
        }
    }

    void end_of_elaboration() override
    {
        QemuCpuArm::end_of_elaboration();

        irq_in.init(m_dev, 0);
        fiq_in.init(m_dev, 1);
        virq_in.init(m_dev, 2);
        vfiq_in.init(m_dev, 3);

        irq_timer_phys_out.init(m_dev, 0);
        irq_timer_virt_out.init(m_dev, 1);
        irq_timer_hyp_out.init(m_dev, 2);
        irq_timer_sec_out.init(m_dev, 3);
    }
};

extern "C" void module_register();
