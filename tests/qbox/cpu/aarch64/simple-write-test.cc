/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <vector>

#include <cci/utils/broker.h>
#include <libgsutils.h>

#include "test/cpu.h"
#include "test/tester/mmio.h"

#include "cortex-a53.h"
#include "qemu-instance.h"

/*
 * Simple ARM Cortex-A53 write test.
 *
 * The CPU starts by fetching its SMP affinity number, then writes ten times to
 * the address (i * 8) of the tester, with i == affinity num, incrementing the
 * value each time. Once it reaches 10, it goes into sleep by calling wfi,
 * effectively starving the kernel and ending the simulation.
 *
 * On each write, the test bench checks the written value. It also checks the
 * number of write at the end of the simulation.
 */
class CpuArmCortexA53SimpleWriteTest : public CpuArmTestBench<cpu_arm_cortexA53, CpuTesterMmio>
{
public:
    static constexpr int NUM_WRITES = 10;

protected:
    std::vector<int> m_writes;
    gs::async_event m_aev;

public:
    CpuArmCortexA53SimpleWriteTest(const sc_core::sc_module_name& n)
        : CpuArmTestBench<cpu_arm_cortexA53, CpuTesterMmio>(n), m_aev("aev")
    {
        m_aev.async_attach_suspending();
        load_firmware_binary(FIRMWARE_BIN_PATH, MEM_ADDR,
                             { static_cast<uint64_t>(CpuTesterMmio::MMIO_ADDR), static_cast<uint64_t>(NUM_WRITES) });

        m_writes.resize(p_num_cpu);
        for (int i = 0; i < p_num_cpu; i++) {
            m_writes[i] = 0;
        }
    }

    virtual ~CpuArmCortexA53SimpleWriteTest() {}

    virtual void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) override
    {
        int cpuid = addr >> 3;

        SCP_INFO(SCMOD) << "CPU " << cpuid << " write at 0x" << std::hex << addr << ", data: " << std::hex << data;

        TEST_ASSERT(cpuid < p_num_cpu);
        TEST_ASSERT(data == m_writes[cpuid]);

        m_writes[cpuid]++;
        for (int i = 0; i < p_num_cpu; i++) {
            if (m_writes[i] < NUM_WRITES) return;
        }
        m_aev.async_detach_suspending();
        sc_core::sc_stop();
    }

    virtual void end_of_simulation() override
    {
        CpuArmTestBench<cpu_arm_cortexA53, CpuTesterMmio>::end_of_simulation();

        for (int i = 0; i < p_num_cpu; i++) {
            TEST_ASSERT(m_writes[i] == NUM_WRITES);
        }
    }
};

int sc_main(int argc, char* argv[]) { return run_testbench<CpuArmCortexA53SimpleWriteTest>(argc, argv); }
