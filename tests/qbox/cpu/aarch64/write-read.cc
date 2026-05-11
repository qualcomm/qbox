/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <vector>
#include <scp/report.h>
#include <cci/utils/broker.h>
#include <libgsutils.h>
#include <cciutils.h>
#include "test/cpu.h"
#include "test/tester/mmio.h"

#include "cortex-a53.h"
#include "qemu-instance.h"

/*
 *  ARM Cortex-A53 write/read test.
 *
 * The CPU starts by fetching its SMP affinity number, then writes ten times to
 * the address (i * 8) of the tester, with i == affinity num, incrementing the
 * value each time. Once it reaches 10, it goes into sleep by calling wfi,
 * effectively starving the kernel and ending the simulation.
 *
 * On each write, the test bench checks the written value. It also checks the
 * number of write at the end of the simulation.
 */
class CpuArmCortexA53WriteReadTest : public CpuArmTestBench<cpu_arm_cortexA53, CpuTesterMmio>
{
public:
    static constexpr uint64_t NUM_WRITES = 10 * 1024;

public:
    CpuArmCortexA53WriteReadTest(const sc_core::sc_module_name& n): CpuArmTestBench<cpu_arm_cortexA53, CpuTesterMmio>(n)
    {
        load_firmware_binary(
            FIRMWARE_BIN_PATH, MEM_ADDR,
            { static_cast<uint64_t>(CpuTesterMmio::MMIO_ADDR), static_cast<uint64_t>(CpuTestBench::BULKMEM_ADDR),
              static_cast<uint64_t>(NUM_WRITES / p_num_cpu) });
    }

    virtual ~CpuArmCortexA53WriteReadTest() {}

    virtual void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) override
    {
        int cpuid = addr >> 3;

        if (data == -1) {
            SCP_WARN(SCMOD) << "An Error was reported by the CPU";
            TEST_ASSERT(false);
        }

        SCP_INFO(SCMOD) << "CPU write at 0x" << std::hex << addr << ", data: " << std::hex << data;

        // for CPU to shut down (accelerators may not WFI idle, and if they wake up, they may keep each other awake)
        if (data == 0 && cpuid < p_num_cpu) {
            m_cpus[cpuid].halt_cb(true);
        }
    }

    virtual void end_of_simulation() override
    {
        CpuArmTestBench<cpu_arm_cortexA53, CpuTesterMmio>::end_of_simulation();
    }
};

int sc_main(int argc, char* argv[]) { return run_testbench<CpuArmCortexA53WriteReadTest>(argc, argv); }
