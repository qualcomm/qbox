/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later
 */

#include "test/cpu.h"
#include "test/tester/mmio.h"

#include "hexagon.h"
#include "qemu-instance.h"
#include "hexagon_subsys.h"

/*
 * Hexagon MMIO load/store test.
 */
class CpuHexagonLdStTest : public CpuTestBench<qemu_cpu_hexagon, CpuTesterMmio>
{
    bool passed = false;
    HexagonSubsys sub_a;
    HexagonSubsys sub_b;

protected:
    gs::async_event m_aev;
    gs::async_event m_finish_event;

public:
    CpuHexagonLdStTest(const sc_core::sc_module_name& n)
        : CpuTestBench<qemu_cpu_hexagon, CpuTesterMmio>(n)
        , sub_a("sub_a", m_inst_a)
        , sub_b("sub_b", m_inst_b)
        , m_aev("aev")
        , m_finish_event("finish")
    {
        for (int i = 0; i < m_cpus.size(); i++) {
            auto& cpu = m_cpus[i];
            /* CPUs alternate between the two QEMU instances (cores) */
            (i % 2 ? sub_b : sub_a).attach(cpu, i / 2);
            cpu.p_start_powered_off = (i != 0);
        }
        sub_a.gregs.p_hexagon_start_addr = MEM_ADDR;
        sub_b.gregs.p_hexagon_start_addr = MEM_ADDR;

        m_aev.async_attach_suspending();
        SC_METHOD(finish_test);
        sensitive << m_finish_event;
        dont_initialize();
        load_firmware_binary(FIRMWARE_BIN_PATH, MEM_ADDR,
                             std::initializer_list<uint32_t>{ static_cast<uint32_t>(CpuTesterMmio::MMIO_ADDR) });
    }

    virtual ~CpuHexagonLdStTest() {}

    void before_end_of_elaboration() override
    {
        CpuTestBench::before_end_of_elaboration();
    }

    virtual void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) override
    {
        SCP_INFO(SCMOD) << "write, data: 0x" << std::hex << data << ", len: 0x" << len;
        passed = (addr == 0 && data == 0x0f0f0f0f && len == sizeof(int32_t));
        m_finish_event.notify(sc_core::SC_ZERO_TIME);
    }

    void finish_test()
    {
        m_aev.async_detach_suspending();
        m_finish_event.async_detach_suspending();
    }

    virtual uint64_t mmio_read(int id, uint64_t addr, size_t len) override { return 0; }

    virtual void end_of_simulation() override
    {
        TEST_ASSERT(passed);
        CpuTestBench<qemu_cpu_hexagon, CpuTesterMmio>::end_of_simulation();
    }
};

int sc_main(int argc, char* argv[]) { return run_testbench<CpuHexagonLdStTest>(argc, argv); }
