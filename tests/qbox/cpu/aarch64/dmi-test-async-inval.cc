/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <cstdlib>
#include <thread>
#include <vector>

#include "test/cpu.h"
#include "test/tester/dmi_soak.h"

#include "cortex-a53.h"
#include "qemu-instance.h"

/*
 * Arm Cortex-A53 DMI async invalidation test.
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
class CpuArmCortexA53DmiAsyncInvalTest : public CpuArmTestBench<cpu_arm_cortexA53, CpuTesterDmiSoak>
{
public:
    static constexpr uint64_t NUM_WRITES = 10000;

private:
    /*
     * Decrease the number of write per CPU when the number of CPUs increases
     * so that the test does not last forever.
     */
    uint64_t m_num_write_per_cpu;
    std::thread m_thread;
    bool running = 0;

public:
    CpuArmCortexA53DmiAsyncInvalTest(const sc_core::sc_module_name& n)
        : CpuArmTestBench<cpu_arm_cortexA53, CpuTesterDmiSoak>(n)
    {
        SCP_DEBUG(SCMOD) << "CpuArmCortexA53DmiAsyncInvalTest constructor";
        m_num_write_per_cpu = NUM_WRITES / (p_num_cpu * 2);

        uint64_t rng_seed = ((static_cast<uint64_t>(std::rand()) << 32) | std::rand());
        load_firmware_binary(
            FIRMWARE_BIN_PATH, MEM_ADDR,
            { static_cast<uint64_t>(CpuTesterDmiSoak::MMIO_ADDR), static_cast<uint64_t>(CpuTesterDmiSoak::DMI_ADDR),
              static_cast<uint64_t>(CpuTesterDmiSoak::DMI_SIZE - 1), rng_seed, m_num_write_per_cpu });
    }

    virtual void start_of_simulation() override
    {
        running = true;
        m_thread = std::thread([&]() { inval(); });
    }
    void inval()
    {
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            uint64_t l = CpuTesterDmiSoak::DMI_SIZE;
            uint64_t s = (std::rand() + 1u) % l;
            l -= s;
            uint64_t e = s + ((std::rand() + 1u) % l);
            if (running) {
                SCP_INFO(SCMOD) << "INVALIDATING";
                m_tester.dmi_invalidate(s, e);
                SCP_INFO(SCMOD) << "Invalidation done";
            }
        }
    }

    virtual ~CpuArmCortexA53DmiAsyncInvalTest() {}

    virtual void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) override
    {
        int cpuid = addr >> 3;

        if (id != CpuTesterDmiSoak::SOCKET_MMIO) {
            SCP_INFO(SCMOD) << "NON DMI write data: 0x" << std::hex << data << ", len: 0x" << len;

            return;
        }

        SCP_INFO(SCMOD) << "cpu_" << cpuid << " write at 0x" << std::hex << addr << " data:0x" << data << ", len: 0x"
                        << len;
        if (data == 0) {
            SCP_INFO(SCMOD) << "cpu_" << cpuid << " DONE";
            // for CPU to shut down (accelerators may not WFI idle, and if they wake up, they may keep each other awake)
            m_cpus[cpuid].halt_cb(true);
        }
        TEST_ASSERT(data != -1);
        TEST_ASSERT(data != -2);
    }

    virtual uint64_t mmio_read(int id, uint64_t addr, size_t len) override
    {
        int cpuid = addr >> 3;

        /* No read on the control socket */
        TEST_ASSERT(id == CpuTesterDmiSoak::SOCKET_DMI);
        SCP_INFO(SCMOD) << "CPU NON DMI read at 0x" << std::hex << addr;

        /* The return value is ignored by the tester */
        return 0;
    }

    virtual bool dmi_request(int id, uint64_t addr, size_t len, tlm::tlm_dmi& ret) override
    {
        SCP_INFO(SCMOD) << "DMI request at " << addr << ", len: " << len;
        return true;
    }

    virtual void end_of_simulation() override
    {
        CpuArmTestBench<cpu_arm_cortexA53, CpuTesterDmiSoak>::end_of_simulation();
        running = false;
        m_thread.join();
    }
};

int sc_main(int argc, char* argv[]) { return run_testbench<CpuArmCortexA53DmiAsyncInvalTest>(argc, argv); }
