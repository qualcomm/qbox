// Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
// SPDX-License-Identifier: BSD-3-Clause

// SA8775P CDSP0 subsystem configuration test
//
// Verifies that all memory-mapped devices in the SA8775P CDSP0 subsystem are
// correctly placed at their expected addresses and have proper reset values.
// Also verifies L2VIC interrupt wiring via soft IRQ.
//
// The test uses a minimal baremetal hexagon firmware (assembled at build time
// via llvm-mc) that reads device version/ID registers and writes results to an
// MMIO probe. The C++ harness then verifies all values.

#include <cstring>
#include <fstream>
#include <vector>

#include <systemc>
#include <cci/utils/broker.h>

#include "async_event.h"
#include "argparser.h"
#include "cciutils.h"
#include "gs_memory.h"
#include "router.h"
#include "scp/report.h"
#include "tlm_utils/tlm_quantumkeeper.h"

#include "test/test.h"
#include "mmio_probe.h"
#include "hexagon.h"
#include "hexagon_globalreg.h"
#include "hexagon-l2vic.h"
#include "hexagon_tlb.h"
#include "qemu_hexagon_qtimer.h"
#include "qemu_pl011.h"

#include "qcom_ipcc.h"
#include "qcom_tcsr.h"
#include "qcom_dspss_pub.h"
#include "qcom_wdog.h"
#include "qcom_turing_rsc.h"
#include "qcom_turing_gdscr.h"
#include "qcom_cdsp0_clkctl.h"
#include "qcom_hwkm_prng.h"
#include "qcom_turing_pll.h"
#include "qcom_qdsp6ss_clkctl.h"

// Device base addresses (SA8775P CDSP0 memory map)
static constexpr uint64_t IPCC_BASE = 0x00400000;
static constexpr uint64_t IPCC_SIZE = 0xC0000;
static constexpr uint64_t HWKM_PRNG_BASE = 0x010DA000;
static constexpr uint64_t HWKM_PRNG_SIZE = 0x1000;
static constexpr uint64_t GDSCR_BASE = 0x00151000;
static constexpr uint64_t GDSCR_SIZE = 0x2000;
static constexpr uint64_t TCSR_BASE = 0x01FC0000;
static constexpr uint64_t TCSR_SIZE = 0x100000;
static constexpr uint64_t UART_BASE = 0x10000000;
static constexpr uint64_t UART_SIZE = 0x1000;
static constexpr uint64_t CORE0_PLL_BASE = 0x26000000;
static constexpr uint64_t CORE0_PLL_SIZE = 0x1000;
static constexpr uint64_t CLKCTL_BASE = 0x26008000;
static constexpr uint64_t CLKCTL_SIZE = 0x20000;
static constexpr uint64_t CSR_BASE = 0x26300000;
static constexpr uint64_t DSPSS_PUB_BASE = CSR_BASE;
static constexpr uint64_t DSPSS_PUB_SIZE = 0x3000;
static constexpr uint64_t CDSP_PLL_BASE = CSR_BASE + 0x40000;
static constexpr uint64_t CDSP_PLL_SIZE = 0x1000;
static constexpr uint64_t QDSP6SS_CLKCTL_BASE = CSR_BASE + 0x48000;
static constexpr uint64_t QDSP6SS_CLKCTL_SIZE = 0x1000;
static constexpr uint64_t WDOG_BASE = CSR_BASE + 0x84000;
static constexpr uint64_t WDOG_SIZE = 0x400;
static constexpr uint64_t L2VIC_BASE = CSR_BASE + 0x90000;
static constexpr uint64_t L2VIC_SIZE = 0x1000;
static constexpr uint64_t L2VIC_FAST_BASE = 0x241E0000;
static constexpr uint64_t L2VIC_FAST_SIZE = 0x10000;
static constexpr uint64_t QTIMER_BASE = CSR_BASE + 0xA1000;
static constexpr uint64_t QTIMER_SIZE = 0x3000;
static constexpr uint64_t TURING_RSC_BASE = 0x260A4000;
static constexpr uint64_t TURING_RSC_SIZE = 0x10000;
static constexpr uint64_t CFGTABLE_BASE = 0x24180000;
static constexpr uint64_t CFGTABLE_SIZE = 512; // 128 words × 4 bytes

// MMIO probe address (for firmware -> C++ result communication)
static constexpr uint64_t PROBE_BASE = 0x80000000;

// Firmware memory
static constexpr uint64_t FW_BASE = 0x0;
static constexpr size_t FW_MEM_SIZE = 256 * 1024;

// Expected device register values (from QEMU qtest reference)
static constexpr uint32_t IPCC_VERSION_EXPECTED = 0x10200;
static constexpr uint32_t TCSR_HW_VERSION_EXPECTED = 0xE8750100;
static constexpr uint32_t DSPSS_VERSION_EXPECTED = 0x10020000;
static constexpr uint32_t WDOG_MAGIC_EXPECTED = 0xDEADBEEF;
static constexpr uint32_t RSC_ID_EXPECTED = 0x00020400;
static constexpr uint32_t PLL_LVAL_EXPECTED = 0x10; // reset default in turing-qdsp6ss-pll
static constexpr uint32_t DONE_MARKER = 0xCAFECAFE;

// Expected config table values (raw entries read via memw_phys)
static constexpr uint32_t CFG_SUBSYSTEM_BASE_EXPECTED = 0x2638;
static constexpr uint32_t CFG_FASTL2VIC_BASE_EXPECTED = 0x241e;
static constexpr uint32_t CFG_JTLB_SIZE_EXPECTED = 0x80;
static constexpr uint32_t CFG_VTCM_SIZE_KB_EXPECTED = 0x2000;
static constexpr uint32_t CFG_THREAD_ENABLE_MASK_EXPECTED = 0x3f;

// Config table ROM data (from QEMU SA8775P_cdsp0.cfgtable, struct field order)
// Layout matches hexagon_config_table union in hexagon.h (128 × uint32_t)
static constexpr uint32_t cfgtable_data[128] = {
    0x00002400, // l2tcm_base
    0x00000000, // reserved0
    0x00002638, // subsystem_base
    0x00002419, // etm_base
    0x0000241a, // l2cfg_base
    0x0000241b, // reserved1
    0x00002500, // l1s0_base
    0x00000000, // axi2_lowaddr
    0x00000000, // streamer_base
    0x00000000, // reserved2
    0x0000241e, // fastl2vic_base
    0x00000080, // jtlb_size_entries
    0x00000001, // coproc_present
    0x00000004, // ext_contexts
    0x00002500, // vtcm_base
    0x00002000, // vtcm_size_kb
    0x00000400, // l2tag_size
    0x00000400, // l2ecomem_size
    0x0000003f, // thread_enable_mask
    0x0000241f, // eccreg_base
    0x00000080, // l2line_size
    0x00000000, // tiny_core
    0x00000000, // l2itcm_size
    0x00002400, // l2itcm_base
    0x00000000, // reserved3
    0x00000000, // dtm_present
    0x00000003, // dma_version
    0x00000007, // hvx_vec_log_length
    0x00000000, // core_id
    0x00000000, // core_count
    0x00000040, // coproc2_reg0
    0x00000020, // coproc2_reg1
    0x00000001, // v2x_mode
    0x00000008, // coproc2_reg2
    0x00000020, // coproc2_reg3
    0x00000000, // coproc2_reg4
    0x00000002, // coproc2_reg5
    0x00000016, // coproc2_reg6
    0x00000006, // coproc2_reg7
    0x00000001, // acd_preset
    0x00000000, // mnd_preset
    0x00000010, // l1d_size_kb
    0x00000020, // l1i_size_kb
    0x00000002, // l1d_write_policy
    0x00000080, // vtcm_bank_width
    0x00000001, // reserved4
    0x00000000, // reserved5
    0x00000003, // reserved6
    0x0000000a, // coproc2_cvt_mpy_size
    0x000000e0, // consistency_domain
    0x00000080, // capacity_domain
    0x00000000, // axi3_lowaddr
    0x00000002, // coproc2_int8_subcolumns
    0x00000005, // corecfg_present
    0x00000007, // coproc2_fp16_acc_exp
    0x00000000, // AXIM2_secondary_base
    0x00000000, // AXIM_data_width
    0x00000000, // l1d_ways
    0x00000000, // l1i_ways
    0x00000000, // l2way_num
    0x00000000, // dma_jtlb_entries
    // Remaining entries (61-127) are zero-initialized
};

// Test slot IDs (firmware writes result at PROBE_BASE + slot*4)
enum TestSlot {
    SLOT_IPCC_VERSION = 0,
    SLOT_TCSR_HW_VERSION,
    SLOT_DSPSS_PUB_VERSION,
    SLOT_WDOG_MAGIC,
    SLOT_RSC_ID,
    SLOT_CDSP_PLL_LVAL,
    SLOT_CORE0_PLL_LVAL,
    SLOT_L2VIC_ENABLE_RESET,
    SLOT_L2VIC_PENDING_BASELINE,
    SLOT_L2VIC_PENDING_SOFT_IRQ,
    SLOT_CFG_SUBSYSTEM_BASE,
    SLOT_CFG_FASTL2VIC_BASE,
    SLOT_CFG_JTLB_SIZE,
    SLOT_CFG_VTCM_SIZE,
    SLOT_CFG_THREAD_ENABLE,
    SLOT_DONE,
    NUM_SLOTS
};

// Firmware is assembled at build time from sa8775p-cdsp0-fw.s via llvm-mc.
// It reads device registers, writes results to an MMIO probe, then loops
// back to _start (via wait + jump) to allow interrupt propagation verification
// across iterations.

class Sa8775pCdsp0SubsysTest : public sc_core::sc_module, public MmioWriter, public MmioReader
{
    SCP_LOGGER();

private:
    static constexpr uint32_t QUANTUM_NS = 1000000; // 1ms quantum

    // QEMU instance
    QemuInstanceManager m_inst_mgr;
    QemuInstance m_inst;

    // CPU and core components
    hexagon_globalreg m_gregs;
    hexagon_l2vic m_l2vic;
    hexagon_tlb m_tlb;
    qemu_hexagon_qtimer m_qtimer;
    qemu_cpu_hexagon m_cpu;

    // Peripherals (all at SA8775P CDSP0 addresses)
    qemu_pl011 m_uart;
    qcom_ipcc m_ipcc;
    qcom_tcsr m_tcsr;
    qcom_dspss_pub m_dspss_pub;
    qcom_wdog m_wdog;
    qcom_turing_rsc m_rsc;
    qcom_turing_gdscr m_gdscr;
    qcom_cdsp0_clkctl m_clkctl;
    qcom_hwkm_prng m_prng;
    qcom_turing_pll m_cdsp_pll;
    qcom_turing_pll m_core0_pll;
    qcom_qdsp6ss_clkctl m_qdsp6ss_clkctl;

    // Memory and routing
    gs::gs_memory<> m_ram;
    gs::gs_memory<> m_cfgtable_rom;
    gs::router<> m_router;

    // Test infrastructure
    MmioProbe m_probe;
    uint32_t m_results[NUM_SLOTS] = {};
    bool m_first_done = false;   // first iteration complete (device results frozen)
    bool m_irq_verified = false; // soft IRQ seen in a subsequent iteration
    gs::async_event m_done_ev;

public:
    Sa8775pCdsp0SubsysTest(const sc_core::sc_module_name& n)
        : sc_core::sc_module(n)
        , m_inst("qemu_inst", &m_inst_mgr, qemu_cpu_hexagon::ARCH)
        , m_gregs("gregs", m_inst, &m_qtimer, &m_l2vic)
        , m_l2vic("l2vic", m_inst)
        , m_tlb("tlb", m_inst)
        , m_qtimer("qtimer", m_inst)
        , m_cpu("cpu", m_inst, &m_gregs, &m_l2vic, &m_tlb)
        , m_uart("uart", m_inst)
        , m_ipcc("ipcc", m_inst)
        , m_tcsr("tcsr", m_inst)
        , m_dspss_pub("dspss_pub", m_inst)
        , m_wdog("wdog", m_inst)
        , m_rsc("rsc", m_inst)
        , m_gdscr("gdscr", m_inst)
        , m_clkctl("clkctl", m_inst)
        , m_prng("prng", m_inst)
        , m_cdsp_pll("cdsp_pll", m_inst)
        , m_core0_pll("core0_pll", m_inst)
        , m_qdsp6ss_clkctl("qdsp6ss_clkctl", m_inst)
        , m_ram("ram")
        , m_cfgtable_rom("cfgtable_rom")
        , m_router("router")
        , m_probe("probe", m_router)
    {
        // Set global quantum
        sc_core::sc_time global_quantum(QUANTUM_NS, sc_core::SC_NS);
        tlm_utils::tlm_quantumkeeper::set_global_quantum(global_quantum);

        // CPU configuration
        m_gregs.p_hexagon_start_addr = FW_BASE;
        m_gregs.p_config_table_addr = CFGTABLE_BASE;

        // TLB configuration
        m_tlb.p_num_entries = 128;

        // Router: CPU initiator
        m_router.add_initiator(m_cpu.socket);

        // Router: map devices (default priority 0 = higher than RAM at priority 1)
        m_router.add_target(m_ram.socket, FW_BASE, FW_MEM_SIZE);
        m_router.add_target(m_ipcc.socket, IPCC_BASE, IPCC_SIZE);
        m_router.add_target(m_prng.socket, HWKM_PRNG_BASE, HWKM_PRNG_SIZE);
        m_router.add_target(m_gdscr.socket, GDSCR_BASE, GDSCR_SIZE);
        m_router.add_target(m_tcsr.socket, TCSR_BASE, TCSR_SIZE);
        m_router.add_target(m_uart.socket, UART_BASE, UART_SIZE);
        m_router.add_target(m_core0_pll.socket, CORE0_PLL_BASE, CORE0_PLL_SIZE);
        m_router.add_target(m_clkctl.socket, CLKCTL_BASE, CLKCTL_SIZE);
        m_router.add_target(m_dspss_pub.socket, DSPSS_PUB_BASE, DSPSS_PUB_SIZE);
        m_router.add_target(m_cdsp_pll.socket, CDSP_PLL_BASE, CDSP_PLL_SIZE);
        m_router.add_target(m_qdsp6ss_clkctl.socket, QDSP6SS_CLKCTL_BASE, QDSP6SS_CLKCTL_SIZE);
        m_router.add_target(m_wdog.socket, WDOG_BASE, WDOG_SIZE);
        m_router.add_target(m_l2vic.socket, L2VIC_BASE, L2VIC_SIZE);
        m_router.add_target(m_l2vic.socket_fast, L2VIC_FAST_BASE, L2VIC_FAST_SIZE);
        m_router.add_target(m_qtimer.view_socket, QTIMER_BASE, QTIMER_SIZE);
        m_router.add_target(m_rsc.socket, TURING_RSC_BASE, TURING_RSC_SIZE);
        m_router.add_target(m_cfgtable_rom.socket, CFGTABLE_BASE, CFGTABLE_SIZE);

        // IRQ wiring (matching SA8775P CDSP0 machine definition)
        m_uart.irq_out(m_l2vic.irq_in[15]);
        m_ipcc.irq_out[6](m_l2vic.irq_in[30]);
        m_rsc.error_irq(m_l2vic.irq_in[41]);
        m_rsc.amc_irq(m_l2vic.irq_in[61]);
        m_qtimer.irq[0](m_l2vic.irq_in[3]);
        m_qtimer.irq[1](m_l2vic.irq_in[4]);

        // L2VIC outputs -> CPU
        for (int i = 0; i < 8; i++) {
            m_l2vic.irq_out[i](m_cpu.irq_in[i]);
        }

        // Load firmware from pre-assembled binary
        {
            std::ifstream fw_file(FIRMWARE_BIN_PATH, std::ios::binary | std::ios::ate);
            if (!fw_file) {
                SCP_FATAL(()) << "Failed to open firmware: " << FIRMWARE_BIN_PATH;
            }
            size_t fw_size = fw_file.tellg();
            fw_file.seekg(0);
            std::vector<uint8_t> fw_data(fw_size);
            fw_file.read(reinterpret_cast<char*>(fw_data.data()), fw_size);
            m_ram.load.ptr_load(fw_data.data(), FW_BASE, fw_size);
        }

        // Load config table ROM data
        m_cfgtable_rom.load.ptr_load(reinterpret_cast<uint8_t*>(const_cast<uint32_t*>(cfgtable_data)), 0,
                                     sizeof(cfgtable_data));

        // Connect MMIO probe callbacks
        m_probe.connect_to_mmio_writer(this);
        m_probe.connect_to_mmio_reader(this);

        m_done_ev.async_attach_suspending();
    }

    void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) override
    {
        uint32_t slot = static_cast<uint32_t>(addr / 4);
        if (slot >= NUM_SLOTS) return;

        uint32_t val = static_cast<uint32_t>(data);

        if (!m_first_done) {
            // First iteration: capture all results
            m_results[slot] = val;
            SCP_INFO(()) << "Slot " << slot << ": 0x" << std::hex << val;
        } else {
            // Subsequent iterations: only update IRQ-related slots
            if (slot == SLOT_L2VIC_PENDING_SOFT_IRQ) {
                if (val & 0x20) {
                    m_irq_verified = true;
                    m_results[slot] = val;
                }
            }
        }

        if (slot == SLOT_DONE && val == DONE_MARKER) {
            if (!m_first_done) {
                m_first_done = true;
            } else if (m_irq_verified) {
                // IRQ results confirmed, simulation can end
                m_done_ev.async_detach_suspending();
            }
        }
    }

    uint64_t mmio_read(int id, uint64_t addr, size_t len) override { return 0; }

    void end_of_simulation() override
    {
        // Verify done marker received
        TEST_ASSERT(m_first_done);
        TEST_ASSERT(m_results[SLOT_DONE] == DONE_MARKER);

        // Verify device version/ID registers
        SCP_INFO(()) << "Checking IPCC version: 0x" << std::hex << m_results[SLOT_IPCC_VERSION];
        TEST_ASSERT(m_results[SLOT_IPCC_VERSION] == IPCC_VERSION_EXPECTED);

        SCP_INFO(()) << "Checking TCSR HW version: 0x" << std::hex << m_results[SLOT_TCSR_HW_VERSION];
        TEST_ASSERT(m_results[SLOT_TCSR_HW_VERSION] == TCSR_HW_VERSION_EXPECTED);

        SCP_INFO(()) << "Checking DSPSS_PUB version: 0x" << std::hex << m_results[SLOT_DSPSS_PUB_VERSION];
        TEST_ASSERT(m_results[SLOT_DSPSS_PUB_VERSION] == DSPSS_VERSION_EXPECTED);

        SCP_INFO(()) << "Checking WDOG magic: 0x" << std::hex << m_results[SLOT_WDOG_MAGIC];
        TEST_ASSERT(m_results[SLOT_WDOG_MAGIC] == WDOG_MAGIC_EXPECTED);

        SCP_INFO(()) << "Checking RSC ID: 0x" << std::hex << m_results[SLOT_RSC_ID];
        TEST_ASSERT(m_results[SLOT_RSC_ID] == RSC_ID_EXPECTED);

        SCP_INFO(()) << "Checking CDSP PLL L_VAL: 0x" << std::hex << m_results[SLOT_CDSP_PLL_LVAL];
        TEST_ASSERT(m_results[SLOT_CDSP_PLL_LVAL] == PLL_LVAL_EXPECTED);

        SCP_INFO(()) << "Checking Core0 PLL L_VAL: 0x" << std::hex << m_results[SLOT_CORE0_PLL_LVAL];
        TEST_ASSERT(m_results[SLOT_CORE0_PLL_LVAL] == PLL_LVAL_EXPECTED);

        // Verify L2VIC reset state
        SCP_INFO(()) << "Checking L2VIC enable at reset: 0x" << std::hex << m_results[SLOT_L2VIC_ENABLE_RESET];
        TEST_ASSERT(m_results[SLOT_L2VIC_ENABLE_RESET] == 0);

        // Verify no spurious interrupts at baseline
        SCP_INFO(()) << "Checking L2VIC pending baseline: 0x" << std::hex << m_results[SLOT_L2VIC_PENDING_BASELINE];
        TEST_ASSERT(m_results[SLOT_L2VIC_PENDING_BASELINE] == 0);

        // Verify L2VIC soft interrupt (bit 5) was triggered.
        // Due to COROUTINE mode timing, the write may take effect in a subsequent
        // loop iteration rather than immediately.
        SCP_INFO(()) << "Checking L2VIC soft IRQ (bit 5): 0x" << std::hex << m_results[SLOT_L2VIC_PENDING_SOFT_IRQ];
        TEST_ASSERT(m_irq_verified);
        TEST_ASSERT((m_results[SLOT_L2VIC_PENDING_SOFT_IRQ] & 0x20) != 0);

        // Verify config table values read via memw_phys
        SCP_INFO(()) << "Checking cfg subsystem_base: 0x" << std::hex << m_results[SLOT_CFG_SUBSYSTEM_BASE];
        TEST_ASSERT(m_results[SLOT_CFG_SUBSYSTEM_BASE] == CFG_SUBSYSTEM_BASE_EXPECTED);

        SCP_INFO(()) << "Checking cfg fastl2vic_base: 0x" << std::hex << m_results[SLOT_CFG_FASTL2VIC_BASE];
        TEST_ASSERT(m_results[SLOT_CFG_FASTL2VIC_BASE] == CFG_FASTL2VIC_BASE_EXPECTED);

        SCP_INFO(()) << "Checking cfg jtlb_size: 0x" << std::hex << m_results[SLOT_CFG_JTLB_SIZE];
        TEST_ASSERT(m_results[SLOT_CFG_JTLB_SIZE] == CFG_JTLB_SIZE_EXPECTED);

        SCP_INFO(()) << "Checking cfg vtcm_size_kb: 0x" << std::hex << m_results[SLOT_CFG_VTCM_SIZE];
        TEST_ASSERT(m_results[SLOT_CFG_VTCM_SIZE] == CFG_VTCM_SIZE_KB_EXPECTED);

        SCP_INFO(()) << "Checking cfg thread_enable_mask: 0x" << std::hex << m_results[SLOT_CFG_THREAD_ENABLE];
        TEST_ASSERT(m_results[SLOT_CFG_THREAD_ENABLE] == CFG_THREAD_ENABLE_MASK_EXPECTED);

        SCP_INFO(()) << "All subsystem configuration checks passed";
    }
};

int sc_main(int argc, char* argv[])
{
    scp::LoggingGuard logging_guard(scp::LogConfig()
                                        .fileInfoFrom(sc_core::SC_ERROR)
                                        .logAsync(false)
                                        .logLevel(scp::log::INFO)
                                        .msgTypeFieldWidth(30));

    gs::ConfigurableBroker broker{};
    cci::cci_originator originator{ "sc_main" };
    auto broker_handle = broker.create_broker_handle(originator);
    ArgParser arg_parser{ broker_handle, argc, argv };

    // Use COROUTINE mode for deterministic write-then-read ordering
    broker_handle.set_preset_cci_value("test.qemu_inst.tcg_mode", cci::cci_value(std::string("COROUTINE")));
    broker_handle.set_preset_cci_value("test.qemu_inst.sync_policy", cci::cci_value(std::string("tlm2")));

    Sa8775pCdsp0SubsysTest test("test");

    // Run for up to 200ms - enough for device reads plus 2+ firmware iterations
    // to verify interrupt propagation. In non-icount COROUTINE mode, simulated
    // time tracks wall-clock, so a generous budget is needed to accommodate
    // varying host speeds.
    sc_core::sc_start(sc_core::sc_time(200, sc_core::SC_MS));
    if (sc_core::SC_STOPPED != sc_core::sc_get_status()) {
        sc_core::sc_stop();
    }

    return 0;
}
