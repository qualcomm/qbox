/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2020
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "cpu.h"

class QemuCpuArm : public QemuCpu
{
public:
    static constexpr qemu::Target ARCH = qemu::Target::AARCH64;

    TargetSignalSocket<bool> power_on;

    QemuCpuArm(const sc_core::sc_module_name& name, QemuInstance& inst, const std::string& type_name)
        : QemuCpu(name, inst, type_name), power_on("power_on")
    {
        auto poweroncb = std::bind(&QemuCpuArm::power_on_cb, this, std::placeholders::_1);
        power_on.register_value_changed_cb(poweroncb);
    }

    /// @returns a new `CpuAarch64` wrapper around `m_cpu`. While this looks like
    /// a copy, it behaves like a downcast. The underlying qemu object reference
    /// count is incremented and decremented correctly by the destructor.
    qemu::CpuAarch64 get_cpu_aarch64() const { return qemu::CpuAarch64(m_cpu); }

    void end_of_elaboration() override
    {
        QemuCpu::end_of_elaboration();

        // This is needed for KVM otherwise the GIC won't reset properly when a system reset is requested
        get_cpu_aarch64().register_reset();
    }

    void start_of_simulation() override
    {
        // According to qemu/hw/arm/virt.c:
        // virt_cpu_post_init() must be called after the CPUs have been realized
        // and the GIC has been created.
        // I am not sure if this the right place where to call it. I wonder if a
        // `before_start_of_simulation` stage would be a better place for this.
        // It MUST be called before `QemuCpu::start_of_simulation()`.
        get_cpu_aarch64().post_init();

        QemuCpu::start_of_simulation();
    }

private:
    void power_on_cb(const bool& val)
    {
        int ret;
        auto arm_cpu = get_cpu_aarch64();

        m_inst.get().lock_iothread();

        if (val) {
            SCP_INFO(()) << "Power ON request for CPU";
            ret = arm_cpu.arm_set_cpu_on_and_reset();
        } else {
            SCP_INFO(()) << "Power OFF request for CPU";
            ret = arm_cpu.arm_set_cpu_off();
        }

        m_inst.get().unlock_iothread();
        m_qemu_kick_ev.async_notify();

        switch (ret) {
        case 0: // QEMU_ARM_POWERCTL_RET_SUCCESS
            SCP_INFO(()) << "arm power control success: CPU Power " << (val ? "ON" : "OFF");
            break;
        case -2: // QEMU_ARM_POWERCTL_INVALID_PARAM
            SCP_WARN(()) << "arm power control failed: Invalid Parameters (CPU not found)";
            break;
        case -3: // QEMU_ARM_POWERCTL_IS_OFF — arm_set_cpu_off only
            SCP_WARN(()) << "arm_set_cpu_off failed: CPU Already OFF";
            break;
        case -4: // QEMU_ARM_POWERCTL_ALREADY_ON — arm_set_cpu_on_and_reset only
            SCP_WARN(()) << "arm_set_cpu_on_and_reset failed: CPU Already ON";
            break;
        case -5: // QEMU_ARM_POWERCTL_ON_PENDING — arm_set_cpu_on_and_reset only
            SCP_WARN(()) << "arm_set_cpu_on_and_reset failed: CPU Power ON is pending";
            break;
        default:
            SCP_FATAL(()) << "arm power control: unexpected return code " << ret;
            __builtin_unreachable();
        }
    }
};
