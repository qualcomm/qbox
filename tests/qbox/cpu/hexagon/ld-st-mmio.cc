/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later
 */

#include <cstdio>
#include <vector>
#include <deque>

#include "test/cpu.h"
#include "test/tester/mmio.h"

#include "hexagon.h"
#include "qemu-instance.h"
#include "hexagon_globalreg.h"

/*
 * Hexagon MMIO load/store test.
 */
class CpuHexagonLdStTest : public CpuTestBench<qemu_cpu_hexagon, CpuTesterMmio>
{
    bool passed = false;
    hexagon_globalreg hex_gregs;

    static constexpr const char* FIRMWARE = R"(
_start:
    r0 = #0x%08)" PRIx32 R"(

    //load and discard the result from CpuTesterMmio::MMIO_ADDR:
    r2 = memw(r0)

    //store a known value to CpuTesterMmio::MMIO_ADDR:
    r3 = #0x0f0f0f0f
    memw(r0) = r3
end:
    // This instruction is not supported by keystone:
    //   wait(r0)
    // So we'll just bypass it and use the word we got from a reference
    //   assembler:
    .word 0x6440c000
    jump end
    )";

protected:
    gs::async_event m_aev;

public:
    CpuHexagonLdStTest(const sc_core::sc_module_name& n)
        : CpuTestBench<qemu_cpu_hexagon, CpuTesterMmio>(n), hex_gregs("hexagon_globalreg", &m_inst_a), m_aev("aev")
    {
        for (int i = 0; i < m_cpus.size(); i++) {
            auto& cpu = m_cpus[i];
            cpu.p_hexagon_num_threads = m_cpus.size();
            cpu.p_start_powered_off = (i != 0);
        }
        hex_gregs.p_hexagon_start_addr = MEM_ADDR;

        char buf[1024];
        m_aev.async_attach_suspending();
        std::snprintf(buf, sizeof(buf), FIRMWARE, static_cast<uint32_t>(CpuTesterMmio::MMIO_ADDR));
        set_firmware(buf, MEM_ADDR);
    }

    virtual ~CpuHexagonLdStTest() {}

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

    virtual void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) override
    {
        SCP_INFO(SCMOD) << "write, data: 0x" << std::hex << data << ", len: 0x" << len;
        passed = (addr == 0 && data == 0x0f0f0f0f && len == sizeof(int32_t));
        m_aev.async_detach_suspending();
    }

    virtual uint64_t mmio_read(int id, uint64_t addr, size_t len) override { return 0; }

    virtual void end_of_simulation() override
    {
        TEST_ASSERT(passed);
        CpuTestBench<qemu_cpu_hexagon, CpuTesterMmio>::end_of_simulation();
    }
};

int sc_main(int argc, char* argv[]) { return run_testbench<CpuHexagonLdStTest>(argc, argv); }
