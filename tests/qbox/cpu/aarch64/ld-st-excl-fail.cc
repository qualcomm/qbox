/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include "test/cpu.h"
#include "test/tester/exclusive.h"

#include "cortex-a53.h"
#include "qemu-instance.h"

/*
 * Arm Cortex-A53 Load/Store exclusive pair failure.
 *
 * Each CPU tries to lock a memory region which has been pre-locked at the
 * beginning of the test. The corresponding stxr should fail in all cases.
 */
class CpuArmCortexA53LdStExclTest : public CpuArmTestBench<cpu_arm_cortexA53, CpuTesterExclusive>
{
    bool passed = true;

public:
    CpuArmCortexA53LdStExclTest(const sc_core::sc_module_name& n)
        : CpuArmTestBench<cpu_arm_cortexA53, CpuTesterExclusive>(n)
    {
        load_firmware_binary(FIRMWARE_BIN_PATH, MEM_ADDR, { static_cast<uint64_t>(CpuTesterExclusive::MMIO_ADDR) });
    }

    virtual ~CpuArmCortexA53LdStExclTest() {}

    virtual void end_of_elaboration() override { m_tester.lock_region_64(0); }

    virtual void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) override
    {
        int cpuid = addr >> 3;

        SCP_INFO(SCMOD) << "FAILED : CPU " << cpuid << " write, data: " << std::hex << data << ", len: " << len;
        passed = false;
    }

    virtual uint64_t mmio_read(int id, uint64_t addr, size_t len) override { return 0; }

    virtual void end_of_simulation() override
    {
        TEST_ASSERT(passed);
        CpuArmTestBench<cpu_arm_cortexA53, CpuTesterExclusive>::end_of_simulation();
    }
};

int sc_main(int argc, char* argv[]) { return run_testbench<CpuArmCortexA53LdStExclTest>(argc, argv); }
