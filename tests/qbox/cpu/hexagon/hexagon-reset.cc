/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later
 */

#include <thread>

#include "async_event.h"
#include "test/cpu.h"
#include "test/tester/mmio.h"
#include <libqemu-cxx/libqemu-cxx.h>

#include "hexagon.h"
#include "qemu-instance.h"
#include "reset_gpio.h"
#include "hexagon_globalreg.h"

/*
 * Hexagon RESET test.
 * Debugging:
 *  -p 'test-bench.inst_a.qemu_args="-d in_asm -monitor -S -s tcp:127.0.0.1:5000,server,nowait"'
 *
 */
class CpuHexagonResetGPIOTest : public CpuTestBench<qemu_cpu_hexagon, CpuTesterMmio>
{
    static constexpr int RESET_TRIGGER = 1;
    static constexpr int RESET_DONE = 0;
    /*
     * There is a 100 second general timeout but if we start running and do not
     * finish in less than 20 seconds there is an issue.  This test should
     * finish in less than 100ms.
     */
    static constexpr int TIMEOUT_LIMIT_MS = 20000;

    // Firmware binary is roughly 40 bytes; load spans a small range we need to
    // invalidate on reset so QEMU re-fetches the updated code.
    static constexpr uint64_t FIRMWARE_SPAN = 64;

    MultiInitiatorSignalSocket<bool> reset;
    reset_gpio reset_controller;
    hexagon_globalreg hex_gregs;
    std::thread m_thread;
    gs::async_event reset_event;
    int reset_count;
    int reset_done;
    int time_elapsed_ms;

    void load_reset_firmware(uint32_t trigger_val)
    {
        load_firmware_binary(FIRMWARE_BIN_PATH, MEM_ADDR,
                             std::initializer_list<uint32_t>{ static_cast<uint32_t>(CpuTesterMmio::MMIO_ADDR),
                                                              trigger_val, static_cast<uint32_t>(RESET_DONE) });
    }

public:
    CpuHexagonResetGPIOTest(const sc_core::sc_module_name& n)
        : CpuTestBench<qemu_cpu_hexagon, CpuTesterMmio>(n)
        , reset_controller("reset", &m_inst_a)
        , hex_gregs("hexagon_globalreg", &m_inst_a)
        , reset_count(0)
        , reset_done(0)
        , time_elapsed_ms(0)
    {
        for (int i = 0; i < m_cpus.size(); i++) {
            auto& cpu = m_cpus[i];
            cpu.p_hexagon_num_threads = m_cpus.size();
            cpu.p_start_powered_off = (i != 0);
        }
        hex_gregs.p_hexagon_start_addr = 0x0;

        reset.bind(reset_controller.reset_in);

        SC_METHOD(reset_method);
        sensitive << reset_event;
        dont_initialize();

        load_reset_firmware(RESET_TRIGGER);
    }

    void before_end_of_elaboration() override
    {
        CpuTestBench::before_end_of_elaboration();
        hex_gregs.before_end_of_elaboration();
        qemu::Device hex_gregs_dev = hex_gregs.get_qemu_dev();
        for (int i = 0; i < m_cpus.size(); i++) {
            auto& cpu = m_cpus[i];
            cpu.before_end_of_elaboration();
            qemu::Device cpu_dev = cpu.get_qemu_dev();
            cpu_dev.set_prop_link("global-regs", hex_gregs_dev);
        }
    }

    virtual ~CpuHexagonResetGPIOTest() {}

    virtual void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) override
    {
        switch (data) {
        case RESET_TRIGGER:
            TEST_ASSERT(reset_count++ == 0);
            /*
             * Load the final firmware image, now the image will write to this address again
             * with a RESET_DONE value and then wait.
             */
            load_reset_firmware(RESET_DONE);
            /*
             * Invalidate translation blocks for the firmware region so QEMU re-fetches the new code.
             * Without this, QEMU would execute stale cached instructions after reset.
             */
            m_inst_a.get().tb_invalidate_phys_range(MEM_ADDR, MEM_ADDR + FIRMWARE_SPAN);
            /*
             * Triggers the reset, this is setup via "sensitive << reset_event" above
             */
            reset_event.notify();
            break;
        case RESET_DONE:
            /*
             * This confirms that we have been reset with the updated firmware image and ran enough
             * code afterward to get here.  This is the beginning of the end of the test.
             */
            reset_done = 1;
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
            TEST_ASSERT(time_elapsed_ms++ < TIMEOUT_LIMIT_MS);
        }
    }
    void reset_method()
    {
        TEST_ASSERT(reset_count == 1);
        reset.async_write_vector({ 1, 0 });
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
        CpuTestBench<qemu_cpu_hexagon, CpuTesterMmio>::end_of_simulation();
    }
};

int sc_main(int argc, char* argv[]) { return run_testbench<CpuHexagonResetGPIOTest>(argc, argv); }
