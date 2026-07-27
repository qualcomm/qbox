/*
 *  This file is part of libqbox
 * Copyright(c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <string>

#include <libqemu-cxx/target/aarch64.h>

#include <module_factory_registery.h>
#include <ports/qemu-target-signal-socket.h>
#include <irq-ctrl/armv7m_nvic/include/armv7m-nvic.h>
#include <arm.h>

class cpu_arm_cortexM55 : public QemuCpuArm
{
public:
    static constexpr qemu::Target ARCH = qemu::Target::AARCH64;

    cci::cci_param<bool> p_start_powered_off;
    nvic_armv7m& m_nvic;
    cci::cci_param<uint64_t> p_init_nsvtor;
    cci::cci_param<uint64_t> p_init_svtor;
    QemuTargetSignalSocket irq_in;
    cpu_arm_cortexM55(const sc_core::sc_module_name& name, sc_core::sc_object* o, sc_core::sc_object* nvic)
        : cpu_arm_cortexM55(name, *(dynamic_cast<QemuInstance*>(o)), *(dynamic_cast<nvic_armv7m*>(nvic)))
    {
    }
    cpu_arm_cortexM55(sc_core::sc_module_name name, QemuInstance& inst, nvic_armv7m& nvic)
        : QemuCpuArm(name, inst, "cortex-m55-arm")
        , m_nvic(nvic)
        , p_start_powered_off("start_powered_off", false,
                              "Start and reset the CPU "
                              "in powered-off state")
        , p_init_nsvtor("init_nsvtor", 0ull, "Non Secure Reset vector base address")
        , p_init_svtor("init_svtor", 0ull, "Secure Reset vector base address")
        , irq_in("irq_in")
    {
        m_nvic.irq_out.bind(irq_in);
    }

    void before_end_of_elaboration() override
    {
        QemuCpuArm::before_end_of_elaboration();

        qemu::CpuArm cpu(m_dev);

        cpu.add_nvic_link();
        cpu.set_prop_bool("start-powered-off", p_start_powered_off);
        cpu.set_prop_int("init-nsvtor", p_init_nsvtor);
        cpu.set_prop_int("init-svtor", p_init_svtor);

        /* ensure the nvic is also created */
        m_nvic.before_end_of_elaboration();

        /* setup cpu&nvic links so that we can realize both objects */
        qemu::Device nvic = m_nvic.get_qemu_dev();
        cpu.set_prop_link("nvic", nvic);
        nvic.set_prop_link("cpu", cpu);
    }

    void end_of_elaboration() override
    {
        QemuCpuArm::end_of_elaboration();

        irq_in.init(m_dev, 0);
    }
};

extern "C" void module_register();
