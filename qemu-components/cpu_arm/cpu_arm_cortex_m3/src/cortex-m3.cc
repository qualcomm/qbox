/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <systemc>
#include "armv7m-nvic.h"

#include <cortex-m3.h>

cpu_arm_cortexM3::cpu_arm_cortexM3(const sc_core::sc_module_name& name, sc_core::sc_object* inst)
    : cpu_arm_cortexM3(name, *(dynamic_cast<QemuInstance*>(inst)))
{
}

cpu_arm_cortexM3::cpu_arm_cortexM3(sc_core::sc_module_name name, QemuInstance& inst)
    : QemuCpuArm(name, inst, "armv7m", "cortex-m3-arm-cpu")
    , p_start_powered_off("start_powered_off", true,
                          "Start and reset the CPU "
                          "in powered-off state")
    , p_pmsav7_dregion("pmsav7_dregion", 8ull, "Number of PMSAv7 MPU data regions")
    , p_clock_hz("clock_hz", 25000000ull, "CPU clock frequency")
    , p_num_irq("num_irq", 64ull, "Number of external NVIC IRQ inputs")
    , p_init_nsvtor("init_nsvtor", 0, "Reset vector base address")
    , irq_in("irq_in", p_num_irq.get_value(), [](const char* n, size_t) { return new QemuTargetSignalSocket(n); })
{
}

void cpu_arm_cortexM3::before_end_of_elaboration()
{
    QemuCpuArm::before_end_of_elaboration();

    qemu::Device armv7m_dev = this->get_qemu_dev();

    armv7m_dev.set_prop_string("cpu-type", m_cpu_type);
    armv7m_dev.set_prop_bool("start-powered-off", p_start_powered_off);
    armv7m_dev.set_prop_int("mpu-ns-regions", p_pmsav7_dregion);
    armv7m_dev.set_prop_int("num-irq", p_num_irq);
    armv7m_dev.set_prop_int("init-nsvtor", p_init_nsvtor);

    m_clk = m_inst.get().clock_new(armv7m_dev.get_qemu_obj(), "SYSCLK");
    m_inst.get().clock_set_hz(m_clk, p_clock_hz);
    m_inst.get().qdev_connect_clock_in(armv7m_dev.get_qemu_obj(), "cpuclk", m_clk);
}

void cpu_arm_cortexM3::end_of_elaboration()
{
    QemuDevice::set_sysbus_as_parent_bus();

    QemuCpuArm::end_of_elaboration();

    for (size_t i = 0; i < irq_in.size(); ++i) {
        irq_in[i].init(m_dev, static_cast<int>(i));
    }
}

void module_register() { GSC_MODULE_REGISTER_C(cpu_arm_cortexM3, sc_core::sc_object*); }
