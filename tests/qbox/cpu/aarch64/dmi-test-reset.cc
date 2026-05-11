/*
 * This file is part of libqbox
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <systemc>

#include <cstdio>
#include <vector>
#include <deque>
#include <thread>
#include <fstream>
#include <iostream>

#include "test/cpu.h"
#include "test/tester/dmi.h"

#include "cortex-a53.h"
#include "qemu-instance.h"
#include "reset_gpio.h"
#include "async_event.h"

/*
 * ARM Cortex-A53 DMI test with reset functionality.
 *
 * This test combines DMI operations with periodic resets triggered by CPU #0.
 * CPU #0 performs DMI read/write operations and triggers a reset after a certain
 * number of iterations. Other CPUs continue DMI operations until reset occurs.
 * The test tracks the number of successful resets and completes after N resets
 * with successful DMI activity restoration.
 */
class CpuArmCortexA53DmiResetTest : public CpuArmTestBench<cpu_arm_cortexA53, CpuTesterDmi>
{
public:
    static constexpr int DMI_WRITES_PER_RESET = 10; // DMI writes before reset (reduced for ARM immediate)
    static constexpr int NUM_RESETS = 3;            // Number of resets to perform
    static constexpr int RESET_TRIGGER_VAL = 7;     // Special value to trigger reset (very small)

    // Firmware is loaded from binary file instead of embedded string

private:
    MultiInitiatorSignalSocket<bool> reset;
    reset_gpio reset_controller;
    std::thread m_thread;
    gs::async_event reset_event;

    int m_reset_count = 0;
    int m_cpu0_iterations = 0;
    bool m_test_completed = false;
    std::mutex m_state_mutex;

public:
    CpuArmCortexA53DmiResetTest(const sc_core::sc_module_name& n)
        : CpuArmTestBench<cpu_arm_cortexA53, CpuTesterDmi>(n), reset_controller("reset", &m_inst_a)
    {
        char buf[2048];
        SCP_DEBUG(SCMOD) << "CpuArmCortexA53DmiResetTest constructor";

        // Configure CPUs
        for (int i = 0; i < m_cpus.size(); i++) {
            auto& cpu = m_cpus[i];
            cpu.p_start_powered_off = false;
        }

        reset.bind(reset_controller.reset_in);

        SC_METHOD(reset_method);
        sensitive << reset_event;
        dont_initialize();

        // Load firmware from binary file
        load_dmi_reset_firmware();
    }

    virtual ~CpuArmCortexA53DmiResetTest() {}

private:
    void load_dmi_reset_firmware()
    {
        load_firmware_binary(
            FIRMWARE_BIN_PATH, MEM_ADDR,
            { static_cast<uint64_t>(CpuTesterDmi::MMIO_ADDR), static_cast<uint64_t>(CpuTesterDmi::DMI_ADDR) });
    }

    virtual void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) override
    {
        int cpuid = (addr >= CpuTesterDmi::MMIO_ADDR) ? ((addr - CpuTesterDmi::MMIO_ADDR) >> 3) : 0;

        if (id != CpuTesterDmi::SOCKET_MMIO) {
            SCP_INFO(SCMOD) << "cpu_" << cpuid << " DMI write data: " << std::hex << data << ", len: " << len;
            return;
        }

        SCP_INFO(SCMOD) << "MMIO: CPU " << cpuid << " wrote " << std::dec << data << " to addr 0x" << std::hex << addr;
        SCP_INFO(SCMOD) << "Total MMIO writes so far: " << (++m_cpu0_iterations);

        // Check for failure indication
        if (data == 9) { // Small failure indicator value
            SCP_FATAL(SCMOD) << "CPU " << cpuid << " reported test failure!";
            TEST_ASSERT(false);
        }

        std::lock_guard<std::mutex> lock(m_state_mutex);

        if (cpuid == 0 && data == RESET_TRIGGER_VAL) { // Only CPU #0 can trigger resets
            m_reset_count++;
            SCP_INFO(SCMOD) << "CPU #0 triggering reset #" << m_reset_count;

            // Invalidate DMI to ensure clean state before reset
            m_tester.dmi_invalidate();

            // Trigger reset
            reset_event.notify();

            // Check if we've completed all resets
            if (m_reset_count >= NUM_RESETS) {
                SCP_INFO(SCMOD) << "All " << NUM_RESETS << " resets completed - test success!";
                m_test_completed = true;
                // Safer completion for SINGLE threading mode - let monitor thread handle stopping
                // reset_event.async_detach_suspending();
                // sc_core::sc_stop();
            }
        }
    }

    virtual uint64_t mmio_read(int id, uint64_t addr, size_t len) override
    {
        int cpuid = (addr >= CpuTesterDmi::MMIO_ADDR) ? ((addr - CpuTesterDmi::MMIO_ADDR) >> 3) : 0;

        if (id != CpuTesterDmi::SOCKET_MMIO) {
            SCP_INFO(SCMOD) << "cpu_" << cpuid << " DMI read len: " << len;
            return 0;
        }

        return 0;
    }

    virtual bool dmi_request(int id, uint64_t addr, size_t len, tlm::tlm_dmi& ret) override
    {
        int cpuid = (addr >= CpuTesterDmi::MMIO_ADDR) ? ((addr - CpuTesterDmi::MMIO_ADDR) >> 3) : 0;
        SCP_INFO(SCMOD) << "cpu_" << cpuid << " DMI request at 0x" << std::hex << addr << ", len: " << len;
        return true; // Always grant DMI
    }

    void reset_method()
    {
        SCP_INFO(SCMOD) << "Executing GPIO reset";
        reset.async_write_vector({ true, false });
    }

    virtual void start_of_simulation() override
    {
        CpuArmTestBench<cpu_arm_cortexA53, CpuTesterDmi>::start_of_simulation();
        m_thread = std::thread([&]() { monitor_thread(); });
    }

    void monitor_thread()
    {
        // Monitor for test timeout
        for (int i = 0; i < 300; i++) { // 30 second timeout
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            {
                std::lock_guard<std::mutex> lock(m_state_mutex);
                if (m_test_completed) {
                    // Safely stop simulation from monitor thread
                    // This avoids race conditions with SINGLE threading mode
                    sc_core::sc_stop();
                    return;
                }
            }
        }

        SCP_FATAL(SCMOD) << "Test timed out after 30 seconds";
        TEST_ASSERT(false);
    }

    virtual void end_of_simulation() override
    {
        if (m_thread.joinable()) {
            m_thread.join();
        }

        std::lock_guard<std::mutex> lock(m_state_mutex);
        SCP_INFO(SCMOD) << "Test completed with " << m_reset_count << " successful resets";
        TEST_ASSERT(m_reset_count >= NUM_RESETS);

        CpuArmTestBench<cpu_arm_cortexA53, CpuTesterDmi>::end_of_simulation();
    }
};

int sc_main(int argc, char* argv[]) { return run_testbench<CpuArmCortexA53DmiResetTest>(argc, argv); }
