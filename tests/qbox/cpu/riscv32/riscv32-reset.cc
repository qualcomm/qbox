/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later
 */

#include <cstdio>
#include <vector>
#include <deque>
#include <fstream>
#include <iostream>

#include <systemc>

#include "async_event.h"
#include "test/cpu.h"
#include "test/tester/mmio.h"
#include <libqemu-cxx/libqemu-cxx.h>

#include "riscv32.h"
#include "qemu-instance.h"
#include "reset_gpio.h"

/*
 * RISC-V 32-bit RESET test.
 *
 * Debugging:
 *   -p log_level=9 (enables SCP_DEBUG output for detailed tracing)
 *   -p log_level=3 (enables SCP_INFO output for basic operation logging)
 *   -p 'test-bench.inst_a.qemu_args="-d in_asm,int"' (QEMU instruction/interrupt trace)
 */
class CpuRiscv32ResetGPIOTest : public CpuTestBench<cpu_riscv32, CpuTesterMmio>
{
    static constexpr int RESET_TRIGGER = 1;
    static constexpr int RESET_DONE = 0;
    /*
     * There is a 100 second general timeout but if we start running and do not
     * finish in less than 20 seconds there is an issue.  This test should
     * finish in less than 100ms.
     */
    static constexpr int TIMEOUT_LIMIT_MS = 20000;

    MultiInitiatorSignalSocket<bool> reset;
    reset_gpio reset_controller;
    std::thread m_thread;
    gs::async_event reset_event;
    int reset_count;
    int reset_done;
    int time_elapsed_ms;

    /*
     * Load RISC-V 32-bit firmware from binary file compiled with LLVM tools.
     * The binary contains the constants at the end that need to be patched.
     */
    void load_firmware_binary(uint32_t mmio_addr, uint32_t trigger_val, uint32_t done_val)
    {
        SCP_DEBUG(SCMOD) << "load_firmware_binary called with mmio_addr=0x" << std::hex << mmio_addr
                         << ", trigger_val=" << std::dec << trigger_val << ", done_val=" << done_val;
        // Load the compiled firmware binary
        const char* firmware_path = FIRMWARE_BIN_PATH;
        std::ifstream file(firmware_path, std::ios::binary | std::ios::ate);

        if (!file.is_open()) {
            SCP_FATAL(SCMOD) << "Failed to open RISC-V firmware file: " << firmware_path;
            TEST_ASSERT(false);
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> firmware_data(size);
        if (!file.read(reinterpret_cast<char*>(firmware_data.data()), size)) {
            SCP_FATAL(SCMOD) << "Failed to read RISC-V firmware file: " << firmware_path;
            TEST_ASSERT(false);
        }

        // Patch the constants in the binary (last 12 bytes: mmio_addr, trigger_val, done_val)
        TEST_ASSERT(size >= 12);
        uint32_t* mmio_addr_ptr = reinterpret_cast<uint32_t*>(firmware_data.data() + size - 12);
        uint32_t* trigger_val_ptr = reinterpret_cast<uint32_t*>(firmware_data.data() + size - 8);
        uint32_t* done_val_ptr = reinterpret_cast<uint32_t*>(firmware_data.data() + size - 4);

        *mmio_addr_ptr = mmio_addr;
        *trigger_val_ptr = trigger_val;
        *done_val_ptr = done_val;

        // Log firmware being loaded (for verification during testing)
        if (trigger_val == RESET_TRIGGER) {
            SCP_INFO(SCMOD) << "Loading initial RISC-V firmware with trigger_val=" << trigger_val << " from "
                            << firmware_path;
        } else {
            SCP_INFO(SCMOD) << "Loading reset firmware with trigger_val=" << trigger_val << " from " << firmware_path;
        }

        // Load firmware directly into memory without keystone assembly
        SCP_DEBUG(SCMOD) << "Loading RISC-V firmware at 0x" << std::hex << MEM_ADDR << " (MEM_ADDR), size=" << std::dec
                         << size << " bytes";
        m_mem.load.ptr_load(firmware_data.data(), MEM_ADDR, size);

        SCP_DEBUG(SCMOD) << "Firmware loaded successfully";
    }

public:
    CpuRiscv32ResetGPIOTest(const sc_core::sc_module_name& n)
        : CpuTestBench<cpu_riscv32, CpuTesterMmio>(n)
        , reset_controller("reset", &m_inst_a)
        , reset_count(0)
        , reset_done(0)
        , time_elapsed_ms(0)
    {
        for (int i = 0; i < m_cpus.size(); i++) {
            auto& cpu = m_cpus[i];
            // RISC-V CPUs don't have p_start_powered_off property like ARM/Hexagon
            // But they may start in a halted state by default. Let's try to ensure they start.
            // The CPU should start executing at resetvec (configured to 0x0 in riscv32.h)

            // Direct connection to CPU reset (like ARM non-SYSTEMMODE approach)
            reset.bind(cpu.reset);
            SCP_DEBUG(SCMOD) << "Connected reset directly to cpu[" << i << "].reset";
        }

        SC_METHOD(reset_method);
        sensitive << reset_event;
        dont_initialize();

        // Load RISC-V 32-bit binary firmware compiled with LLVM tools
        load_firmware_binary(static_cast<uint32_t>(CpuTesterMmio::MMIO_ADDR), RESET_TRIGGER, RESET_DONE);
    }

    virtual ~CpuRiscv32ResetGPIOTest() {}

    virtual void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) override
    {
        SCP_INFO(SCMOD) << "MMIO write: data=0x" << std::hex << data
                        << " (expect 1=trigger, 0=done), reset_count=" << std::dec << reset_count;

        char buf[1024] = { 0 };
        switch (data) {
        case RESET_TRIGGER:
            // With COROUTINE threading, the CPU may execute the loop multiple times
            // before reset takes effect. Only process the first trigger.
            if (reset_count > 0) {
                SCP_DEBUG(SCMOD) << "Ignoring additional RESET_TRIGGER (reset_count=" << reset_count << ")";
                break;
            }
            reset_count++;
            /*
             * Load the final firmware image, now the image will write to this address again
             * with a RESET_DONE value and then wait.
             */
            SCP_DEBUG(SCMOD) << "About to call load_firmware_binary with RESET_DONE";
            load_firmware_binary(static_cast<uint32_t>(CpuTesterMmio::MMIO_ADDR), RESET_DONE, RESET_DONE);
            SCP_DEBUG(SCMOD) << "load_firmware_binary call completed";

            /*
             * Invalidate TB cache to ensure cached translations of old firmware are cleared
             */
            SCP_DEBUG(SCMOD) << "Invalidating TB cache for address range 0x0 to 0x" << std::hex << (MEM_ADDR + 1024UL);
            m_inst_a.get().tb_invalidate_phys_range(0, MEM_ADDR + 1024);
            m_inst_b.get().tb_invalidate_phys_range(0, MEM_ADDR + 1024);
            SCP_DEBUG(SCMOD) << "TB cache invalidation completed";

            /*
             * Triggers the reset, this is setup via "sensitive << reset_event" above.
             * Use a delta notification (SC_ZERO_TIME) so it fires in the next
             * delta cycle at the current simulation time.  A timed delay (e.g.
             * 1 SC_US) would require SystemC time to advance, which in mcips
             * mode only happens when the sync_window moves forward, leaving
             * the event stranded while the CPU spins in a tight MMIO loop.
             * Firmware loading and TB invalidation are already complete before
             * this point, so no additional delay is needed.
             */
            reset_event.notify(sc_core::SC_ZERO_TIME);
            break;
        case RESET_DONE:
            /*
             * This confirms that we have been reset with the updated firmware image and ran enough
             * code afterward to get here.  This is the beginning of the end of the test.
             */
            reset_done = 1;
            SCP_INFO(SCMOD) << "Reset test completed successfully, stopping simulation";
            sc_core::sc_stop();
            break;
        default:
            TEST_ASSERT(false);
            break;
        }
    }

    virtual uint64_t mmio_read(int id, uint64_t addr, size_t len) override { return 0; }

    void reset_pthread()
    {
        while (1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (reset_done) {
                /*
                 * The detach here allows the system-c to exit.  If we skip
                 * this step sc will just hang.
                 */
                reset_event.async_detach_suspending();
                return;
            }

            // Add debug output every second to track progress
            if (time_elapsed_ms % 1000 == 0) {
                SCP_INFO(SCMOD) << "Waiting for RISC-V CPU execution... time: " << time_elapsed_ms << "ms"
                                << "\n";
            }

            TEST_ASSERT(time_elapsed_ms++ < TIMEOUT_LIMIT_MS);
        }
    }
    void reset_method()
    {
        TEST_ASSERT(reset_count == 1);
        SCP_DEBUG(SCMOD) << "Triggering reset sequence with single pulse";

        // Send reset pulse: assert then immediately deassert
        // Use synchronous timing to ensure proper sequencing with COROUTINE mode
        reset.async_write_vector({ true, false });

        SCP_DEBUG(SCMOD) << "Reset pulse completed";
    }

    virtual void start_of_simulation() override
    {
        m_thread = std::thread([&]() { reset_pthread(); });
    }

    virtual void end_of_simulation() override
    {
        m_thread.join();
        TEST_ASSERT(reset_count == 1);
        TEST_ASSERT(time_elapsed_ms < TIMEOUT_LIMIT_MS);
        SCP_INFO(SCMOD) << "reset_count : " << reset_count << "\n";
        SCP_INFO(SCMOD) << "timeout trip : " << time_elapsed_ms << "\n";
        SCP_INFO(SCMOD) << "PASS\n";
        CpuTestBench<cpu_riscv32, CpuTesterMmio>::end_of_simulation();
    }
};

int sc_main(int argc, char* argv[]) { return run_testbench<CpuRiscv32ResetGPIOTest>(argc, argv); }
