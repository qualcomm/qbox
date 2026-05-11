/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <systemc>

#include <algorithm>
#include <mutex>
#include <vector>

#include "test/cpu.h"
#include "test/tester/dmi.h"

#include "cortex-a53.h"
#include "qemu-instance.h"

/*
 * Arm Cortex-A53 DMI concurrent invalidation test.
 *
 * In this test, all CPUs share the same DMI region. Each CPU does some
 * read/modify/write to its dedicated 64-bits memory area, and at some point
 * invalidate the whole region.
 *
 * This test is quite stressful for the whole DMI sub-system as invalidations
 * are very common and come from every CPU randomly. Each CPU checks that the
 * value it reads from memory is the one it expects, and at the end, the test
 * checks that all dedicated memory areas contain the final value (corresponding
 * to the number of read/modify/write operations the CPUs did).
 */
class CpuArmCortexA53DmiConcurrentInvalTest : public CpuArmTestBench<cpu_arm_cortexA53, CpuTesterDmi>
{
public:
    static constexpr int NUM_WRITES = 1024;

private:
    /*
     * Decrease the number of write per CPU when the number of CPUs increases
     * so that the test does not last forever.
     */

    // would be nice to check how many accesses we perform (skid) after an invalidate hits a CPU.... should be<=1
    int m_num_write_per_cpu;

    std::vector<bool> invalidated;
    std::mutex mutex;

protected:
    std::vector<gs::async_event> m_aevs;

public:
    CpuArmCortexA53DmiConcurrentInvalTest(const sc_core::sc_module_name& n)
        : CpuArmTestBench<cpu_arm_cortexA53, CpuTesterDmi>(n), invalidated(p_num_cpu, false), m_aevs(p_num_cpu)
    {
        SCP_DEBUG(SCMOD) << "CpuArmCortexA53DmiConcurrentInvalTest constructor";
        m_num_write_per_cpu = NUM_WRITES / p_num_cpu;

        load_firmware_binary(
            FIRMWARE_BIN_PATH, MEM_ADDR,
            { static_cast<uint64_t>(CpuTesterDmi::MMIO_ADDR), static_cast<uint64_t>(CpuTesterDmi::DMI_ADDR),
              static_cast<uint64_t>(m_num_write_per_cpu) });
    }

    virtual ~CpuArmCortexA53DmiConcurrentInvalTest() {}

    virtual void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) override
    {
        int cpuid = addr >> 3;

        if (id != CpuTesterDmi::SOCKET_MMIO) {
            SCP_INFO(SCMOD) << "cpu_" << cpuid << " DMI write data: " << std::hex << data << ", len: " << len
                            << " (old value " << m_tester.get_buf_value(cpuid) << ")";

            /*
             * The tester is about to write the new value to memory. Check the
             * old value just in case.
             */
            TEST_ASSERT(m_tester.get_buf_value(cpuid) == data - 1);

            return;
        }

        SCP_INFO(SCMOD) << "cpu_" << cpuid << " b_transport write at 0x" << std::hex << addr << ", data: " << std::hex
                        << data << ", len: " << len;

        // for CPU to shut down (accelerators may not WFI idle, and if they wake up, they may keep each other awake)
        if (data == 0x80000000 && cpuid < p_num_cpu) {
            m_aevs[cpuid].async_detach_suspending();
            return;
        }
        TEST_ASSERT(data != -1); // -1 indicated a fail from the ASM
        int count = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            count = std::count(invalidated.begin(), invalidated.end(), true);
            if (count == 0) {
                std::fill(invalidated.begin(), invalidated.end(), true);
                SCP_INFO(SCMOD) << "Invalidating";
            } else {
                SCP_INFO(SCMOD) << "Already invalidated count " << count << " out of " << p_num_cpu;
            }
        }
        if (count == 0) m_tester.dmi_invalidate();
    }

    virtual uint64_t mmio_read(int id, uint64_t addr, size_t len) override
    {
        int cpuid = addr >> 3;

        /* No read on the control socket */
        TEST_ASSERT(id == CpuTesterDmi::SOCKET_DMI);

        SCP_INFO(SCMOD) << "cpu_" << cpuid << "DMI read data: " << std::hex << m_tester.get_buf_value(cpuid)
                        << ", len: " << len;

        /* The return value is ignored by the tester */
        return 0;
    }

    virtual bool dmi_request(int id, uint64_t addr, size_t len, tlm::tlm_dmi& ret) override
    {
        SCP_INFO(SCMOD) << "cpu_" << (addr >> 3) << " DMI request at 0x" << std::hex << addr << ", len: " << len;
        std::lock_guard<std::mutex> lock(mutex);
        int last = invalidated[addr >> 3];
        invalidated[addr >> 3] = false;
        int c = std::count(invalidated.begin(), invalidated.end(), true);
        if (last && c == 0) m_tester.dmi_invalidate_switch();
        return c == 0;
    }

    virtual void end_of_simulation() override
    {
        CpuArmTestBench<cpu_arm_cortexA53, CpuTesterDmi>::end_of_simulation();

        for (int i = 0; i < p_num_cpu; i++) {
            TEST_ASSERT(m_tester.get_buf_value(i) == ((m_num_write_per_cpu + 3) & (-1ull << 2)));
        }
    }
};

int sc_main(int argc, char* argv[]) { return run_testbench<CpuArmCortexA53DmiConcurrentInvalTest>(argc, argv); }
