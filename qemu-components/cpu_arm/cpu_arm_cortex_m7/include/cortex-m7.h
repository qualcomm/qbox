/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <string>

#include <libqemu-cxx/target/aarch64.h>

#include <module_factory_registery.h>
#include <arm.h>

class cpu_arm_cortexM7 : public QemuCpuArm
{
private:
    qemu::Clock m_clk;

public:
    cci::cci_param<bool> p_start_powered_off;
    cci::cci_param<uint64_t> p_init_nsvtor;
    cci::cci_param<uint64_t> p_clock_hz;

    cpu_arm_cortexM7(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : cpu_arm_cortexM7(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }
    cpu_arm_cortexM7(sc_core::sc_module_name name, QemuInstance& inst)
        : QemuCpuArm(name, inst, "armv7m", "cortex-m7-arm-cpu")
        , p_start_powered_off("start_powered_off", false,
                              "Start and reset the CPU "
                              "in powered-off state")
        , p_init_nsvtor("init_nsvtor", 0ull, "Reset vector base address")
        , p_clock_hz("clock_hz", 25000000ull, "CPU clock frequency")
    {
    }

    void before_end_of_elaboration() override
    {
        QemuCpuArm::before_end_of_elaboration();

        qemu::Device armv7m_dev = this->get_qemu_dev();

        armv7m_dev.set_prop_string("cpu-type", m_cpu_type.c_str());
        armv7m_dev.set_prop_bool("start-powered-off", p_start_powered_off);
        armv7m_dev.set_prop_int("init-nsvtor", p_init_nsvtor);

        m_clk = m_inst.get().clock_new(armv7m_dev.get_qemu_obj(), "SYSCLK");
        m_inst.get().clock_set_hz(m_clk, p_clock_hz);
        m_inst.get().qdev_connect_clock_in(armv7m_dev.get_qemu_obj(), "cpuclk", m_clk);
    }

    void end_of_elaboration() override
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuCpuArm::end_of_elaboration();
    }
};
extern "C" void module_register();
