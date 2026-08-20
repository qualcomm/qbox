/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <string>
#include <cstring>
#include <cstdio>

#include <libqemu-cxx/target/aarch64.h>

#include <module_factory_registery.h>

#include <armv7m-nvic.h>
#include <arm.h>
#include <ports/qemu-target-signal-socket.h>

class cpu_arm_cortexM7 : public QemuCpuArm
{
public:
    cci::cci_param<bool> p_start_powered_off;
    nvic_armv7m& m_nvic;
    cci::cci_param<uint64_t> p_init_nsvtor;
    cci::cci_param<uint64_t> p_pmsav7_dregion;
    cci::cci_param<uint64_t> p_cpu_freq_hz;

    /* CPU IRQ input: m_nvic.irq_out (excpout) → irq_in → CPU ARM_CPU_IRQ */
    QemuTargetSignalSocket irq_in;

    /* Passthrough sockets: router → nvic_socket (target) → nvic_fwd (initiator) → m_nvic.socket */
    tlm_utils::simple_target_socket<cpu_arm_cortexM7> nvic_socket;
    tlm_utils::simple_initiator_socket<cpu_arm_cortexM7> nvic_fwd;

    cpu_arm_cortexM7(const sc_core::sc_module_name& name, sc_core::sc_object* o, sc_core::sc_object* nvic)
        : cpu_arm_cortexM7(name, *(dynamic_cast<QemuInstance*>(o)), *(dynamic_cast<nvic_armv7m*>(nvic)))
    {
    }
    cpu_arm_cortexM7(sc_core::sc_module_name name, QemuInstance& inst, nvic_armv7m& nvic)
        : QemuCpuArm(name, inst, "cortex-m7-arm")
        , m_nvic(nvic)
        , p_start_powered_off("start_powered_off", false,
                              "Start and reset the CPU "
                              "in powered-off state")
        , p_init_nsvtor("init_nsvtor", 0ull, "Reset vector base address")
        , p_pmsav7_dregion("pmsav7_dregion", 8ull,
                           "Number of PMSAv7 MPU data regions (default: 8)")
        , p_cpu_freq_hz("cpu_freq_hz", 100000000ull, "CPU clock frequency in Hz (default: 100 MHz)")
        , irq_in("irq_in")
        , nvic_socket("nvic_socket")
        , nvic_fwd("nvic_fwd")
    {
        /* Register forwarding callback */
        nvic_socket.register_b_transport(this, &cpu_arm_cortexM7::nvic_b_transport);

        /* Bind initiator socket to NVIC's target socket */
        nvic_fwd.bind(m_nvic.socket);

        /* Connect NVIC excpout to CPU ARM_CPU_IRQ */
        m_nvic.irq_out.bind(irq_in);
    }

    /* Forward all NVIC register accesses from target socket to initiator socket */
    void nvic_b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        uint64_t addr = trans.get_address();
        uint64_t offset = addr & 0xFFFF;

        // ICSR is at offset 0xD04 (0xE000ED04 - 0xE000E000).
        // Suppress PENDSVSET (bit 28) on writes: VP does not deliver PendSV synchronously;
        // asynchronous PendSV fires into an inconsistent Zephyr scheduler state.
        if (offset == 0xD04 &&
            trans.get_command() == tlm::TLM_WRITE_COMMAND &&
            trans.get_data_length() >= 4 &&
            trans.get_data_ptr() != nullptr)
        {
            uint32_t val;
            std::memcpy(&val, trans.get_data_ptr(), 4);
            if (val & (1u << 28)) {
                uint32_t masked = val & ~(1u << 28);
                printf("[NVIC] ICSR write: 0x%08X -> masked to 0x%08X"
                       " (PENDSVSET suppressed)\n", val, masked);
                std::memcpy(trans.get_data_ptr(), &masked, 4);
            }
        }

        trans.set_address(offset);
        nvic_fwd->b_transport(trans, delay);
        trans.set_address(addr);
    }

    void before_end_of_elaboration() override
    {
        QemuCpuArm::before_end_of_elaboration();

        qemu::CpuArm cpu(m_dev);

        cpu.add_nvic_link();
        cpu.set_prop_bool("start-powered-off", p_start_powered_off);
        cpu.set_prop_int("init-nsvtor", p_init_nsvtor);
        cpu.set_prop_int("pmsav7-dregion", p_pmsav7_dregion);

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

        /* Wire NVIC excpout to CPU ARM_CPU_IRQ (gpio input 0) */
        irq_in.init(m_dev, 0);
    }

};
extern "C" void module_register();
