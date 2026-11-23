/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "test/cpu.h"
#include "test/tester/mmio.h"
#include "riscv32.h"
#include "clic-riscv.h"
#include <ports/multiinitiator-signal-socket.h>

class CpuRiscv32ClicTest : public CpuTestBench<cpu_riscv32, CpuTesterMmio>
{
    static constexpr uint64_t CONTROL_ADDR = 0x017ff000;
    static constexpr uint64_t INTERRUPT_ADDR = 0x01800000;
    static constexpr unsigned SOURCE = 21;

    clic_riscv m_clic;
    InitiatorSignalSocket<bool> m_source;
    MultiInitiatorSignalSocket<bool> m_reset;
    sc_core::sc_event m_stop;
    unsigned m_stage = 0;
    bool m_failed = false;

    bool expect(bool condition)
    {
        if (!condition) {
            std::cerr << "CLIC protocol assertion failed at stage " << m_stage << '\n';
            m_failed = true;
            m_source->write(false);
            m_stop.notify(sc_core::SC_ZERO_TIME);
        }
        return condition;
    }

    void boot()
    {
        wait(sc_core::SC_ZERO_TIME);
        m_reset.async_write_vector({ true, false });
    }

    void finish() { sc_core::sc_stop(); }

public:
    CpuRiscv32ClicTest(const sc_core::sc_module_name& n)
        : CpuTestBench<cpu_riscv32, CpuTesterMmio>(n), m_clic("clic", m_inst_a), m_source("source"), m_reset("reset")
    {
        TEST_ASSERT(m_cpus.size() == 1);
        m_cpus[0].p_clic = "clic";
        m_clic.irq_out.bind(m_cpus[0].irq_in[27]);
        m_source.bind(m_clic.irq_in[SOURCE]);
        m_reset.bind(m_cpus[0].reset);
        m_router.add_target(m_clic.socket, CONTROL_ADDR, 0x1000);
        m_router.add_target(m_clic.interrupt_socket, INTERRUPT_ADDR, 0x1000);
        load_firmware_binary<uint32_t>(FIRMWARE_BIN_PATH, MEM_ADDR,
                                       { CpuTesterMmio::MMIO_ADDR, CONTROL_ADDR, INTERRUPT_ADDR });

        SC_THREAD(boot);
        SC_METHOD(finish);
        sensitive << m_stop;
        dont_initialize();
    }

    void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) override
    {
        if (!expect(id == CpuTesterMmio::SOCKET_MMIO && len == 4)) {
            return;
        }
        switch (addr) {
        case 0:
            if (!expect(m_stage == 0 && data == 1)) {
                return;
            }
            m_source->write(true);
            m_stage = 1;
            break;
        case 4:
            // Guest has observed pending=1 with delivery enabled at the CPU,
            // but this source disabled, and reports its handler count.
            if (!expect(m_stage == 1 && data == 0)) {
                return;
            }
            m_stage = 2;
            break;
        case 8:
            if (!expect(m_stage == 2 && data == (0x80000000u | SOURCE))) {
                return;
            }
            m_stage = 3;
            break;
        case 12:
            if (!expect(m_stage == 3 && data == 1)) {
                return;
            }
            m_source->write(false);
            m_stage = 4;
            break;
        case 16:
            if (!expect(m_stage == 4 && data == 1)) {
                return;
            }
            m_stage = 5;
            m_stop.notify(sc_core::SC_ZERO_TIME);
            break;
        case 20:
            std::cerr << "Guest check " << data << " failed at protocol stage " << m_stage << '\n';
            expect(false);
            break;
        default:
            expect(false);
            break;
        }
    }

    void end_of_simulation() override
    {
        if (!m_failed && m_stage == 5) {
            SCP_INFO(SCMOD) << "PASS: disabled pending source delivered once after enable; cause=21; pending cleared";
        }
        CpuTestBench<cpu_riscv32, CpuTesterMmio>::end_of_simulation();
    }

    int get_rc() const { return TestBench::get_rc() || m_failed || m_stage != 5; }
};

int sc_main(int argc, char* argv[]) { return run_testbench<CpuRiscv32ClicTest>(argc, argv); }
