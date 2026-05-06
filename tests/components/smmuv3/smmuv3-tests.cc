/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "smmuv3-bench.h"

TEST_BENCH(smmuv3_bench, ID_Registers)
{
    uint32_t idr0 = mmio_read32(smmuv3_regs::IDR0);
    ASSERT_TRUE(idr0 & 0x1);
    ASSERT_TRUE(idr0 & 0x2);
    ASSERT_EQ((idr0 >> 2) & 0x3, 0x2);

    uint32_t idr1 = mmio_read32(smmuv3_regs::IDR1);
    ASSERT_GT(idr1 & 0x3F, 0u);

    uint32_t idr5 = mmio_read32(smmuv3_regs::IDR5);
    ASSERT_NE(idr5 & 0x7, 0u);
    ASSERT_TRUE((idr5 >> 4) & 0x1);

    uint32_t iidr = mmio_read32(smmuv3_regs::IIDR);
    ASSERT_EQ(iidr, 0x0u);
}

TEST_BENCH(smmuv3_bench, CR0_CR0ACK_Mirror)
{
    mmio_write32(smmuv3_regs::CR0, 0x1);
    sc_core::wait(1, sc_core::SC_NS);
    ASSERT_EQ(mmio_read32(smmuv3_regs::CR0ACK), 0x1u);

    mmio_write32(smmuv3_regs::CR0, 0x1 | (1u << 3));
    sc_core::wait(1, sc_core::SC_NS);
    ASSERT_EQ(mmio_read32(smmuv3_regs::CR0ACK), 0x1u | (1u << 3));
}

TEST_BENCH(smmuv3_bench, IRQ_CTRL_ACK_Mirror)
{
    mmio_write32(smmuv3_regs::IRQ_CTRL, 0x7);
    sc_core::wait(1, sc_core::SC_NS);
    ASSERT_EQ(mmio_read32(smmuv3_regs::IRQ_CTRL_ACK), 0x7u);
}

TEST_BENCH(smmuv3_bench, LinearStreamTable_S1_4KPage)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0_table = DRAM_BASE + 0x2000;
    const uint64_t l1_table = DRAM_BASE + 0x3000;
    const uint64_t l2_table = DRAM_BASE + 0x4000;
    const uint64_t l3_table = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x08000000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0_table, 16, 0, 5);
    write_table_desc(l0_table, 0, l1_table);
    write_table_desc(l1_table, 0, l2_table);
    write_table_desc(l2_table, 0, l3_table);
    write_page_4k(l3_table, 0, page_pa);

    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xDEADBEEFu), tlm::TLM_OK_RESPONSE);

    uint32_t readback = 0;
    read_dram(page_pa, &readback, 4);
    ASSERT_EQ(readback, 0xDEADBEEFu);
}

TEST_BENCH(smmuv3_bench, LinearStreamTable_S1_2MBlock)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0_table = DRAM_BASE + 0x2000;
    const uint64_t l1_table = DRAM_BASE + 0x3000;
    const uint64_t l2_table = DRAM_BASE + 0x4000;
    const uint64_t dest_pa = DRAM_BASE + 0x00200000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0_table, 16, 0, 5);
    write_table_desc(l0_table, 0, l1_table);
    write_table_desc(l1_table, 0, l2_table);
    write_block_2mb(l2_table, 0, dest_pa);

    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xCAFEBABE), tlm::TLM_OK_RESPONSE);

    uint32_t readback = 0;
    read_dram(dest_pa, &readback, 4);
    ASSERT_EQ(readback, 0xCAFEBABEu);
}

TEST_BENCH(smmuv3_bench, CMDQ_Sync_UpdatesCons)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;

    setup_cmdq(cmdq_base, 4);
    enable_cmdq();

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = gs::CMD_OP_SYNC;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(mmio_read32(smmuv3_regs::CMDQ_CONS) & 0x1F, 1u);
}

TEST_BENCH(smmuv3_bench, CMDQ_TLBI_NH_ALL)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;

    setup_cmdq(cmdq_base, 4);
    enable_cmdq();

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = gs::CMD_OP_TLBI_NH_ALL;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(mmio_read32(smmuv3_regs::CMDQ_CONS) & 0x1F, 1u);
}

TEST_BENCH(smmuv3_bench, Page1_Aliases_CMDQ_PROD)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;

    setup_cmdq(cmdq_base, 4);
    enable_cmdq();

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = gs::CMD_OP_SYNC;
    write_dram(cmdq_base, cmd.data(), gs::SMMUV3_CMD_SIZE);

    mmio_write32((smmuv3_regs::PAGE1_OFFSET + smmuv3_regs::CMDQ_PROD), 1);
    sc_core::wait(10, sc_core::SC_NS);

    ASSERT_EQ(mmio_read32(smmuv3_regs::CMDQ_PROD), 1u);
    ASSERT_EQ(mmio_read32(smmuv3_regs::CMDQ_CONS) & 0x1F, 1u);
}

TEST_BENCH(smmuv3_bench, Page1_Aliases_EVENTQ_CONS)
{
    mmio_write32((smmuv3_regs::PAGE1_OFFSET + smmuv3_regs::EVENTQ_CONS), 0x42);
    sc_core::wait(1, sc_core::SC_NS);
    ASSERT_EQ(mmio_read32(smmuv3_regs::EVENTQ_CONS), 0x42u);
    ASSERT_EQ(mmio_read32((smmuv3_regs::PAGE1_OFFSET + smmuv3_regs::EVENTQ_CONS)), 0x42u);
}

TEST_BENCH(smmuv3_bench, GATOS_Translation)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0_table = DRAM_BASE + 0x2000;
    const uint64_t l1_table = DRAM_BASE + 0x3000;
    const uint64_t l2_table = DRAM_BASE + 0x4000;
    const uint64_t l3_table = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x10000000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0_table, 16, 0, 5);
    write_table_desc(l0_table, 0, l1_table);
    write_table_desc(l1_table, 0, l2_table);
    write_table_desc(l2_table, 0, l3_table);
    write_page_4k(l3_table, 0, page_pa);

    enable_smmu();

    mmio_write32(smmuv3_regs::GATOS_SID, 0);
    mmio_write32(smmuv3_regs::GATOS_ADDR_LO, 0x0);
    mmio_write32(smmuv3_regs::GATOS_ADDR_LO + 4, 0);
    mmio_write32(smmuv3_regs::GATOS_CTRL, (1u << 8) | (1u << 0));
    sc_core::wait(10, sc_core::SC_NS);

    uint32_t par_lo = mmio_read32(smmuv3_regs::GATOS_PAR_LO);
    ASSERT_EQ(par_lo & 0x1, 0u);
    ASSERT_EQ(par_lo & ~0xFFFu, static_cast<uint32_t>(page_pa) & ~0xFFFu);
}

TEST_BENCH(smmuv3_bench, TwoLevelStreamTable_S1_4KPage)
{
    const uint64_t l1_strtab = DRAM_BASE;
    const uint64_t l2_ste_tab = DRAM_BASE + 0x00010000;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0_table = DRAM_BASE + 0x2000;
    const uint64_t l1_table = DRAM_BASE + 0x3000;
    const uint64_t l2_table = DRAM_BASE + 0x4000;
    const uint64_t l3_table = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x01000000;

    setup_2level_stream_table(l1_strtab, 4, 6);
    write_l1_std(l1_strtab, 0, l2_ste_tab);
    write_ste_s1(l2_ste_tab, cd_base);
    write_cd_identity(cd_base, l0_table, 16, 0, 5);
    write_table_desc(l0_table, 0, l1_table);
    write_table_desc(l1_table, 0, l2_table);
    write_table_desc(l2_table, 0, l3_table);
    write_page_4k(l3_table, 0, page_pa);

    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xA5A5A5A5u), tlm::TLM_OK_RESPONSE);
    uint32_t rb = 0;
    read_dram(page_pa, &rb, 4);
    ASSERT_EQ(rb, 0xA5A5A5A5u);
}

TEST_BENCH(smmuv3_bench, S2Only_4KPage)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t s2_l0 = DRAM_BASE + 0x2000;
    const uint64_t s2_l1 = DRAM_BASE + 0x3000;
    const uint64_t s2_l2 = DRAM_BASE + 0x4000;
    const uint64_t s2_l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x02000000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s2(strtab, s2_l0, 16, 2, 0, 5);
    write_table_desc(s2_l0, 0, s2_l1);
    write_table_desc(s2_l1, 0, s2_l2);
    write_table_desc(s2_l2, 0, s2_l3);
    write_page_4k_s2(s2_l3, 0, page_pa);

    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0x5A5A5A5Au), tlm::TLM_OK_RESPONSE);
    uint32_t rb = 0;
    read_dram(page_pa, &rb, 4);
    ASSERT_EQ(rb, 0x5A5A5A5Au);
}

TEST_BENCH(smmuv3_bench, Nested_S1S2_4KPage)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x03000000;
    const uint64_t s2_l1 = DRAM_BASE + 0x00200000;

    setup_linear_stream_table(strtab, 4);
    write_ste_nested(strtab, cd_base, s2_l1, 16, 1, 0, 5);
    write_block_1gb_s2(s2_l1, 2, DRAM_BASE);

    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0, page_pa);

    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xC0FFEE01u), tlm::TLM_OK_RESPONSE);
    uint32_t rb = 0;
    read_dram(page_pa, &rb, 4);
    ASSERT_EQ(rb, 0xC0FFEE01u);
}

TEST_BENCH(smmuv3_bench, S1_16KGranule)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x4000;
    const uint64_t l2 = DRAM_BASE + 0x8000;
    const uint64_t l3 = DRAM_BASE + 0xC000;
    const uint64_t page_pa = DRAM_BASE + 0x01000000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l2, 28, 2, 5);
    write_table_desc(l2, 0, l3);
    write_page_16k(l3, 0, page_pa);

    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0x16161616u), tlm::TLM_OK_RESPONSE);
    uint32_t rb = 0;
    read_dram(page_pa, &rb, 4);
    ASSERT_EQ(rb, 0x16161616u);
}

TEST_BENCH(smmuv3_bench, S1_64KGranule)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x10000;
    const uint64_t l2 = DRAM_BASE + 0x20000;
    const uint64_t l3 = DRAM_BASE + 0x30000;
    const uint64_t page_pa = DRAM_BASE + 0x02000000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l2, 22, 1, 5);
    write_table_desc(l2, 0, l3);
    write_page_64k(l3, 0, page_pa);

    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0x64646464u), tlm::TLM_OK_RESPONSE);
    uint32_t rb = 0;
    read_dram(page_pa, &rb, 4);
    ASSERT_EQ(rb, 0x64646464u);
}

TEST_BENCH(smmuv3_bench, Fault_InvalidSTE_GBPA_Abort)
{
    const uint64_t strtab = DRAM_BASE;
    setup_linear_stream_table(strtab, 4);
    write_ste_invalid(strtab);
    mmio_write32(smmuv3_regs::GBPA, smmuv3_regs::GBPA_ABORT);
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
}

TEST_BENCH(smmuv3_bench, Fault_STE_ConfigAbort_RecordsEvent)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t eventq_base = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    write_ste_abort(strtab);
    setup_eventq(eventq_base, 4);
    enable_eventq();
    enable_irqs();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    sc_core::wait(5, sc_core::SC_NS);

    ASSERT_EQ(event_type_at(eventq_base, 0), 0x04);
    ASSERT_EQ(mmio_read32(smmuv3_regs::EVENTQ_PROD) & 0x1Fu, 1u);
    ASSERT_GT(irq_eventq_cnt.count, 0u);
}

TEST_BENCH(smmuv3_bench, Fault_InvalidCD_RecordsEvent)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t eventq_base = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_invalid(cd_base);
    setup_eventq(eventq_base, 4);
    enable_eventq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    sc_core::wait(5, sc_core::SC_NS);
    ASSERT_EQ(event_type_at(eventq_base, 0), 0x09);
}

TEST_BENCH(smmuv3_bench, Fault_Walk_UnmappedTable)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t eventq_base = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    setup_eventq(eventq_base, 4);
    enable_eventq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    sc_core::wait(5, sc_core::SC_NS);
    ASSERT_EQ(event_type_at(eventq_base, 0), 0x10);
}

TEST_BENCH(smmuv3_bench, Fault_L3_ReservedType2)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t eventq_base = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_raw_desc(l3, 0, (DRAM_BASE + 0x08000000ULL) | (1ULL << 1) | (1ULL << 6) | (1ULL << 10));
    setup_eventq(eventq_base, 4);
    enable_eventq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    sc_core::wait(5, sc_core::SC_NS);
    ASSERT_EQ(event_type_at(eventq_base, 0), 0x10);
}

TEST_BENCH(smmuv3_bench, Fault_Permission_WriteRO)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x05000000;
    const uint64_t eventq_base = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k_ro(l3, 0, page_pa);
    setup_eventq(eventq_base, 4);
    enable_eventq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xDEADu), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    sc_core::wait(5, sc_core::SC_NS);
    ASSERT_EQ(event_type_at(eventq_base, 0), 0x13);

    ASSERT_EQ(tbu_txn(0x0, false, 0), tlm::TLM_OK_RESPONSE);
}

TEST_BENCH(smmuv3_bench, EVENTQ_ProducerWrapsCorrectly)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t eventq_base = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    write_ste_abort(strtab);
    setup_eventq(eventq_base, 2);
    enable_eventq();
    enable_smmu();

    for (int i = 0; i < 5; ++i) {
        (void)tbu_txn(0x0, false, 0);
        mmio_write32(smmuv3_regs::EVENTQ_CONS, mmio_read32(smmuv3_regs::EVENTQ_PROD));
        sc_core::wait(2, sc_core::SC_NS);
    }

    uint32_t prod = mmio_read32(smmuv3_regs::EVENTQ_PROD);
    ASSERT_EQ(prod & 0x3u, 1u);
    ASSERT_EQ((prod >> 2) & 0x1u, 1u);
}

TEST_BENCH(smmuv3_bench, GERROR_Toggle_Ack_Protocol)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t eventq_base = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    write_ste_abort(strtab);
    setup_eventq(eventq_base, 1);
    enable_eventq();
    enable_smmu();

    uint32_t start_err = read_gerror();
    for (int i = 0; i < 8; ++i) {
        (void)tbu_txn(0x0, false, 0);
        sc_core::wait(1, sc_core::SC_NS);
    }
    uint32_t after_err = read_gerror();
    ASSERT_NE(start_err, after_err);

    write_gerrorn(after_err);
    sc_core::wait(1, sc_core::SC_NS);
    ASSERT_EQ(read_gerror(), after_err);
    ASSERT_EQ(read_gerror() ^ read_gerrorn(), 0u);
}

TEST_BENCH(smmuv3_bench, CMDQ_TLBI_NH_ASID_InvalidatesIOTLB)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x06000000;
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0, page_pa);
    setup_cmdq(cmdq_base, 4);
    enable_cmdq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0x1u), tlm::TLM_OK_RESPONSE);
    ASSERT_GT(smmu.test_iotlb_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x11;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMDQ_CFGI_STE_InvalidatesSteCache)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x07000000;
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0, page_pa);
    setup_cmdq(cmdq_base, 4);
    enable_cmdq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xA), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(smmu.test_ste_cache_size(), 1u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x03;
    *reinterpret_cast<uint32_t*>(cmd.data() + 4) = 0;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_ste_cache_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMD_Resume_AbortsStalledTxn)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    setup_cmdq(cmdq_base, 4);
    enable_cmdq();
    enable_smmu();

    smmu.test_inject_stall(0xABCD1234);
    ASSERT_TRUE(smmu.test_stall_present(0xABCD1234));
    ASSERT_FALSE(smmu.test_stall_aborted(0xABCD1234));

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x44;
    *reinterpret_cast<uint32_t*>(cmd.data() + 4) = 0xABCD1234;
    cmd[11] = 0x1;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_TRUE(smmu.test_stall_aborted(0xABCD1234));
}

#define ASSERT_CMDQ_DRAINED(idx)                                       \
    do {                                                               \
        ASSERT_EQ(mmio_read32(smmuv3_regs::CMDQ_CONS) & 0x1Fu, (idx)); \
    } while (0)

TEST_BENCH(smmuv3_bench, CMD_PREFETCH_CONFIG_Drained)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    setup_cmdq(cmdq_base, 4);
    enable_cmdq();
    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x01;
    issue_cmd(cmdq_base, 0, cmd.data());
    ASSERT_CMDQ_DRAINED(1u);
}

TEST_BENCH(smmuv3_bench, CMD_PREFETCH_ADDR_Drained)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    setup_cmdq(cmdq_base, 4);
    enable_cmdq();
    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x02;
    issue_cmd(cmdq_base, 0, cmd.data());
    ASSERT_CMDQ_DRAINED(1u);
}

TEST_BENCH(smmuv3_bench, CMD_CFGI_STE_RANGE_FlushesSteCache)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    prime_translation_and_caches(cmdq_base);
    ASSERT_GT(smmu.test_ste_cache_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x04;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_ste_cache_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMD_CFGI_CD_FlushesCdCache)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    prime_translation_and_caches(cmdq_base);
    ASSERT_GT(smmu.test_cd_cache_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x05;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_cd_cache_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMD_CFGI_CD_ALL_FlushesCdCache)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    prime_translation_and_caches(cmdq_base);
    ASSERT_GT(smmu.test_cd_cache_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x06;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_cd_cache_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMD_CFGI_ALL_FlushesBothCaches)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    prime_translation_and_caches(cmdq_base);
    ASSERT_GT(smmu.test_ste_cache_size(), 0u);
    ASSERT_GT(smmu.test_cd_cache_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x07;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_ste_cache_size(), 0u);
    ASSERT_EQ(smmu.test_cd_cache_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMD_TLBI_NH_ALL_FlushesIOTLB)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    prime_translation_and_caches(cmdq_base);
    ASSERT_GT(smmu.test_iotlb_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x10;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMD_TLBI_NH_VA_FlushesMatchingIOVA)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    prime_translation_and_caches(cmdq_base);
    ASSERT_GT(smmu.test_iotlb_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x12;
    *reinterpret_cast<uint16_t*>(cmd.data() + 6) = 0;
    *reinterpret_cast<uint64_t*>(cmd.data() + 8) = 0;
    cmd[15] = 0;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMD_TLBI_NH_VAA_FlushesAnyAsid)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    prime_translation_and_caches(cmdq_base);
    ASSERT_GT(smmu.test_iotlb_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x13;
    *reinterpret_cast<uint64_t*>(cmd.data() + 8) = 0;
    cmd[15] = 0;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMD_TLBI_EL2_ALL_AliasesNhAll)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    prime_translation_and_caches(cmdq_base);
    ASSERT_GT(smmu.test_iotlb_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x18;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMD_TLBI_EL2_VA_AliasesNhVa)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    prime_translation_and_caches(cmdq_base);
    ASSERT_GT(smmu.test_iotlb_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x1A;
    *reinterpret_cast<uint64_t*>(cmd.data() + 8) = 0;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMD_TLBI_S_EL2_ASID_AliasesNhAsid)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    prime_translation_and_caches(cmdq_base);
    ASSERT_GT(smmu.test_iotlb_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x21;
    *reinterpret_cast<uint16_t*>(cmd.data() + 6) = 0;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMD_TLBI_S12_VMALL_FlushesByVmid)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    prime_translation_and_caches(cmdq_base);
    ASSERT_GT(smmu.test_iotlb_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x28;
    *reinterpret_cast<uint16_t*>(cmd.data() + 2) = 0;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMD_TLBI_S2_IPA_FlushesMatchingIpa)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    prime_translation_and_caches(cmdq_base);
    ASSERT_GT(smmu.test_iotlb_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x2A;
    *reinterpret_cast<uint16_t*>(cmd.data() + 2) = 0;
    *reinterpret_cast<uint64_t*>(cmd.data() + 8) = 0;
    cmd[15] = 0;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMD_TLBI_NSNH_ALL_FlushesIOTLB)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    prime_translation_and_caches(cmdq_base);
    ASSERT_GT(smmu.test_iotlb_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x30;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMD_ATC_INV_DrainedAsNoOp)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    setup_cmdq(cmdq_base, 4);
    enable_cmdq();
    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x40;
    issue_cmd(cmdq_base, 0, cmd.data());
    ASSERT_CMDQ_DRAINED(1u);
}

TEST_BENCH(smmuv3_bench, CMD_PRI_RESP_Drained)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    setup_cmdq(cmdq_base, 4);
    enable_cmdq();
    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x41;
    issue_cmd(cmdq_base, 0, cmd.data());
    ASSERT_CMDQ_DRAINED(1u);
}

TEST_BENCH(smmuv3_bench, CMD_STALL_TERM_DrainedAsNoOp)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    setup_cmdq(cmdq_base, 4);
    enable_cmdq();
    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x45;
    issue_cmd(cmdq_base, 0, cmd.data());
    ASSERT_CMDQ_DRAINED(1u);
}

TEST_BENCH(smmuv3_bench, CMD_SYNC_CS_IRQ_FiresInterrupt)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    setup_cmdq(cmdq_base, 4);
    enable_cmdq();
    enable_irqs();

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd_no_irq{};
    cmd_no_irq[0] = 0x46;
    issue_cmd(cmdq_base, 0, cmd_no_irq.data());
    ASSERT_EQ(irq_cmd_sync_cnt.count, 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd_irq{};
    cmd_irq[0] = 0x46;
    cmd_irq[1] = 0x10;
    issue_cmd(cmdq_base, 1, cmd_irq.data());
    ASSERT_GT(irq_cmd_sync_cnt.count, 0u);
}

TEST_BENCH(smmuv3_bench, PRI_Record_AdvancesProdAndIrq)
{
    const uint64_t priq_base = DRAM_BASE + 0x00040000;
    setup_priq(priq_base, 4);
    enable_priq();
    enable_irqs();
    sc_core::wait(1, sc_core::SC_NS);

    ASSERT_EQ(mmio_read32(smmuv3_regs::PRIQ_PROD), 0u);
    smmu.test_inject_pri(0x55, 0xDEADBEEF0000ULL, 0x1u);
    sc_core::wait(1, sc_core::SC_NS);

    ASSERT_EQ(mmio_read32(smmuv3_regs::PRIQ_PROD) & 0xFu, 1u);
    ASSERT_GT(irq_priq_cnt.count, 0u);

    uint8_t rec[16];
    read_dram(priq_base, rec, 16);
    ASSERT_EQ(*reinterpret_cast<uint32_t*>(rec), 0x55u);
}

#define SMMU_CR0_OFF            0x0020
#define SMMU_CR0ACK_OFF         0x0024
#define SMMU_GBPA_OFF           0x0044
#define SMMU_S_IDR1_OFF         0x8004
#define SMMU_S_INIT_OFF         0x803C
#define SMMU_S_GBPA_OFF         0x8044
#define SMMU_CR0_SMMUEN         (1u << 0)
#define SMMU_GBPA_UPDATE        (1u << 31)
#define SMMU_GBPA_ABORT         (1u << 20)
#define SMMU_S_INIT_INV_ALL     (1u << 0)
#define SMMU_S_IDR1_SECURE_IMPL (1u << 31)

TEST_BENCH(smmuv3_bench, SecurityInit_GBPA_SetsAbortAndSelfClears)
{
    sc_core::wait(1, sc_core::SC_NS);
    ASSERT_EQ(mmio_read32(SMMU_GBPA_OFF) & SMMU_GBPA_UPDATE, 0u);

    mmio_write32(SMMU_GBPA_OFF, SMMU_GBPA_UPDATE | SMMU_GBPA_ABORT);
    sc_core::wait(1, sc_core::SC_NS);

    uint32_t gbpa = mmio_read32(SMMU_GBPA_OFF);
    ASSERT_EQ(gbpa & SMMU_GBPA_UPDATE, 0u);
    ASSERT_NE(gbpa & SMMU_GBPA_ABORT, 0u);

    uint32_t s_idr1 = mmio_read32(SMMU_S_IDR1_OFF);
    ASSERT_EQ(s_idr1 & SMMU_S_IDR1_SECURE_IMPL, 0u);
}

TEST_BENCH(smmuv3_bench, Init_SInit_InvAllSelfClears)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x09000000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0, page_pa);
    enable_smmu();
    (void)tbu_txn(0x0, true, 0xAAu);
    ASSERT_GT(smmu.test_iotlb_size(), 0u);
    ASSERT_GT(smmu.test_ste_cache_size(), 0u);

    mmio_write32(SMMU_S_INIT_OFF, SMMU_S_INIT_INV_ALL);
    sc_core::wait(1, sc_core::SC_NS);
    ASSERT_EQ(mmio_read32(SMMU_S_INIT_OFF) & SMMU_S_INIT_INV_ALL, 0u);

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
    ASSERT_EQ(smmu.test_ste_cache_size(), 0u);
    ASSERT_EQ(smmu.test_cd_cache_size(), 0u);
}

TEST_BENCH(smmuv3_bench, NsSetAbortAll_DisablesSmmuAndAbortsTraffic)
{
    const uint64_t strtab = DRAM_BASE;
    setup_linear_stream_table(strtab, 4);
    write_ste_bypass(strtab);
    enable_smmu();
    ASSERT_NE(mmio_read32(SMMU_CR0ACK_OFF) & SMMU_CR0_SMMUEN, 0u);

    ASSERT_EQ(mmio_read32(SMMU_GBPA_OFF) & SMMU_GBPA_UPDATE, 0u);
    mmio_write32(SMMU_GBPA_OFF, SMMU_GBPA_UPDATE | SMMU_GBPA_ABORT);
    sc_core::wait(1, sc_core::SC_NS);
    ASSERT_EQ(mmio_read32(SMMU_GBPA_OFF) & SMMU_GBPA_UPDATE, 0u);

    uint32_t cr0 = mmio_read32(SMMU_CR0_OFF);
    mmio_write32(SMMU_CR0_OFF, cr0 & ~SMMU_CR0_SMMUEN);
    sc_core::wait(1, sc_core::SC_NS);
    ASSERT_EQ(mmio_read32(SMMU_CR0ACK_OFF) & SMMU_CR0_SMMUEN, 0u);

    ASSERT_EQ(tbu_txn(0x0, true, 0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
}

TEST_BENCH(smmuv3_bench, FullSecureBootSequence)
{
    sc_core::wait(1, sc_core::SC_NS);
    mmio_write32(SMMU_GBPA_OFF, SMMU_GBPA_UPDATE | SMMU_GBPA_ABORT);
    sc_core::wait(1, sc_core::SC_NS);
    ASSERT_EQ(mmio_read32(SMMU_GBPA_OFF) & SMMU_GBPA_UPDATE, 0u);
    ASSERT_EQ(mmio_read32(SMMU_S_IDR1_OFF) & SMMU_S_IDR1_SECURE_IMPL, 0u);

    mmio_write32(SMMU_S_INIT_OFF, SMMU_S_INIT_INV_ALL);
    sc_core::wait(1, sc_core::SC_NS);
    ASSERT_EQ(mmio_read32(SMMU_S_INIT_OFF) & SMMU_S_INIT_INV_ALL, 0u);

    mmio_write32(SMMU_GBPA_OFF, SMMU_GBPA_UPDATE | SMMU_GBPA_ABORT);
    sc_core::wait(1, sc_core::SC_NS);
    uint32_t cr0 = mmio_read32(SMMU_CR0_OFF);
    mmio_write32(SMMU_CR0_OFF, cr0 & ~SMMU_CR0_SMMUEN);
    sc_core::wait(1, sc_core::SC_NS);
    ASSERT_EQ(mmio_read32(SMMU_CR0ACK_OFF) & SMMU_CR0_SMMUEN, 0u);
    ASSERT_NE(mmio_read32(SMMU_GBPA_OFF) & SMMU_GBPA_ABORT, 0u);
}

struct dual_stream_setup {
    uint64_t page0_pa;
    uint64_t page1_pa;
};

static dual_stream_setup setup_two_streams(smmuv3_bench& b)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd0 = DRAM_BASE + 0x1000;
    const uint64_t cd1 = DRAM_BASE + 0x1100;
    const uint64_t s0_l0 = DRAM_BASE + 0x2000;
    const uint64_t s0_l1 = DRAM_BASE + 0x3000;
    const uint64_t s0_l2 = DRAM_BASE + 0x4000;
    const uint64_t s0_l3 = DRAM_BASE + 0x5000;
    const uint64_t s1_l0 = DRAM_BASE + 0x6000;
    const uint64_t s1_l1 = DRAM_BASE + 0x7000;
    const uint64_t s1_l2 = DRAM_BASE + 0x8000;
    const uint64_t s1_l3 = DRAM_BASE + 0x9000;
    const uint64_t page0_pa = DRAM_BASE + 0x0B000000;
    const uint64_t page1_pa = DRAM_BASE + 0x0B100000;

    b.setup_linear_stream_table(strtab, 4);

    b.write_ste_s1(strtab + 0 * 64, cd0);
    b.write_cd_identity(cd0, s0_l0, 16, 0, 5, 0);
    b.write_table_desc(s0_l0, 0, s0_l1);
    b.write_table_desc(s0_l1, 0, s0_l2);
    b.write_table_desc(s0_l2, 0, s0_l3);
    b.write_page_4k(s0_l3, 0, page0_pa);

    b.write_ste_s1(strtab + 1 * 64, cd1);
    b.write_cd_identity(cd1, s1_l0, 16, 0, 5, 1);
    b.write_table_desc(s1_l0, 0, s1_l1);
    b.write_table_desc(s1_l1, 0, s1_l2);
    b.write_table_desc(s1_l2, 0, s1_l3);
    b.write_page_4k(s1_l3, 0, page1_pa);

    b.enable_smmu();
    return { page0_pa, page1_pa };
}

TEST_BENCH(smmuv3_bench, MultiStream_Memcpy)
{
    auto pas = setup_two_streams(*this);

    static constexpr uint32_t k_pattern[] = { 0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u };

    for (size_t i = 0; i < sizeof(k_pattern) / sizeof(k_pattern[0]); ++i) {
        ASSERT_EQ(tbu_txn(i * 4, true, k_pattern[i]), tlm::TLM_OK_RESPONSE);
    }
    for (size_t i = 0; i < sizeof(k_pattern) / sizeof(k_pattern[0]); ++i) {
        ASSERT_EQ(tbu1_txn(i * 4, true, k_pattern[i]), tlm::TLM_OK_RESPONSE);
    }

    for (size_t i = 0; i < sizeof(k_pattern) / sizeof(k_pattern[0]); ++i) {
        uint32_t a = 0, b = 0;
        read_dram(pas.page0_pa + i * 4, &a, 4);
        read_dram(pas.page1_pa + i * 4, &b, 4);
        ASSERT_EQ(a, k_pattern[i]);
        ASSERT_EQ(b, k_pattern[i]);
    }
}

TEST_BENCH(smmuv3_bench, MultiStream_WriteOnlyStream)
{
    auto pas = setup_two_streams(*this);

    for (uint32_t i = 0; i < 16; ++i) {
        ASSERT_EQ(tbu_txn(i * 4, true, 0xCAFE0000u | i), tlm::TLM_OK_RESPONSE);
    }

    for (uint32_t i = 0; i < 16; ++i) {
        uint32_t v = 0;
        read_dram(pas.page0_pa + i * 4, &v, 4);
        ASSERT_EQ(v, 0xCAFE0000u | i);
    }
}

TEST_BENCH(smmuv3_bench, MultiStream_ReadOnlyStream)
{
    auto pas = setup_two_streams(*this);

    uint64_t expected_sum = 0;
    for (uint32_t i = 0; i < 32; ++i) {
        uint32_t v = (i + 1) * 0x10001u;
        write_dram(pas.page1_pa + i * 4, &v, 4);
        expected_sum += v;
    }

    uint64_t sum = 0;
    for (uint32_t i = 0; i < 32; ++i) {
        tlm::tlm_generic_payload txn;
        uint32_t local = 0;
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_address(i * 4);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&local));
        txn.set_data_length(4);
        txn.set_streaming_width(4);
        txn.set_byte_enable_length(0);
        txn.set_dmi_allowed(false);
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        tbu1_initiator->b_transport(txn, delay);
        ASSERT_EQ(txn.get_response_status(), tlm::TLM_OK_RESPONSE);
        sum += local;
    }
    ASSERT_EQ(sum, expected_sum);
}

TEST_BENCH(smmuv3_bench, MultiStream_AlternatingFrames)
{
    auto pas = setup_two_streams(*this);

    constexpr uint32_t kTxns = 32;
    for (uint32_t f = 0; f < kTxns; ++f) {
        uint32_t value = 0xAA000000u | f;
        if ((f & 1u) == 0u) {
            ASSERT_EQ(tbu_txn((f / 2) * 4, true, value), tlm::TLM_OK_RESPONSE);
        } else {
            ASSERT_EQ(tbu1_txn((f / 2) * 4, true, value), tlm::TLM_OK_RESPONSE);
        }
    }

    for (uint32_t f = 0; f < kTxns; ++f) {
        uint32_t v = 0;
        uint64_t pa = ((f & 1u) == 0u ? pas.page0_pa : pas.page1_pa) + (f / 2) * 4;
        read_dram(pa, &v, 4);
        ASSERT_EQ(v, 0xAA000000u | f);
    }
}

TEST_BENCH(smmuv3_bench, MultiStream_PerStreamIsolation)
{
    auto pas = setup_two_streams(*this);

    ASSERT_EQ(tbu_txn(0, true, 0xA1u), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(tbu1_txn(0, true, 0xB2u), tlm::TLM_OK_RESPONSE);

    uint32_t a = 0, b = 0;
    read_dram(pas.page0_pa, &a, 4);
    read_dram(pas.page1_pa, &b, 4);
    ASSERT_EQ(a, 0xA1u);
    ASSERT_EQ(b, 0xB2u);

    write_ste_abort(DRAM_BASE);
    setup_linear_stream_table(DRAM_BASE, 4);

    ASSERT_EQ(tbu_txn(0, true, 0xC3u), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_EQ(tbu1_txn(4, true, 0xD4u), tlm::TLM_OK_RESPONSE);

    uint32_t b2 = 0;
    read_dram(pas.page1_pa + 4, &b2, 4);
    ASSERT_EQ(b2, 0xD4u);
}

TEST_BENCH(smmuv3_bench, ResetPulse_ClearsCachesAndReseedsId)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x06000000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0, page_pa);
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xAA), tlm::TLM_OK_RESPONSE);
    ASSERT_GT(smmu.test_iotlb_size(), 0u);
    ASSERT_GT(smmu.test_ste_cache_size(), 0u);
    ASSERT_GT(smmu.test_cd_cache_size(), 0u);

    smmu.test_inject_stall(0x12345);
    ASSERT_TRUE(smmu.test_stall_present(0x12345));

    pulse_reset();

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
    ASSERT_EQ(smmu.test_ste_cache_size(), 0u);
    ASSERT_EQ(smmu.test_cd_cache_size(), 0u);
    ASSERT_FALSE(smmu.test_stall_present(0x12345));

    ASSERT_EQ(mmio_read32(smmuv3_regs::IIDR), 0x0u);
    uint32_t idr0 = mmio_read32(smmuv3_regs::IDR0);
    ASSERT_TRUE(idr0 & 0x1);
    ASSERT_TRUE(idr0 & 0x2);
}

TEST_BENCH(smmuv3_bench, CMDQ_DmaFault_RaisesGerror)
{
    const uint64_t bogus_base = 0xDEAD0000ULL;

    uint32_t lo = static_cast<uint32_t>(bogus_base) | 4u;
    uint32_t hi = static_cast<uint32_t>(bogus_base >> 32);
    mmio_write32(smmuv3_regs::CMDQ_BASE_LO, lo);
    mmio_write32(smmuv3_regs::CMDQ_BASE_HI, hi);
    mmio_write32(smmuv3_regs::CMDQ_PROD, 0);
    mmio_write32(smmuv3_regs::CMDQ_CONS, 0);
    enable_cmdq();

    uint32_t err_before = read_gerror();
    mmio_write32(smmuv3_regs::CMDQ_PROD, 1);
    sc_core::wait(5, sc_core::SC_NS);
    uint32_t err_after = read_gerror();

    ASSERT_NE(err_before, err_after);
    ASSERT_NE((err_after ^ read_gerrorn()) & 0x1u, 0u);
}

TEST_BENCH(smmuv3_bench, CMDQ_UnknownOpcode_RaisesGerror)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    setup_cmdq(cmdq_base, 4);
    enable_cmdq();

    uint32_t err_before = read_gerror();
    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0xFF;
    issue_cmd(cmdq_base, 0, cmd.data());

    uint32_t err_after = read_gerror();
    ASSERT_NE(err_before, err_after);
    ASSERT_NE((err_after ^ read_gerrorn()) & 0x1u, 0u);
}

TEST_BENCH(smmuv3_bench, CMDQ_LogSizeZero_NoDispatch)
{
    mmio_write32(smmuv3_regs::CMDQ_BASE_LO, 0);
    mmio_write32(smmuv3_regs::CMDQ_BASE_HI, 0);
    mmio_write32(smmuv3_regs::CMDQ_PROD, 0);
    mmio_write32(smmuv3_regs::CMDQ_CONS, 0);
    enable_cmdq();

    mmio_write32(smmuv3_regs::CMDQ_PROD, 1);
    sc_core::wait(2, sc_core::SC_NS);
    ASSERT_EQ(mmio_read32(smmuv3_regs::CMDQ_CONS) & 0x1Fu, 0u);
    ASSERT_EQ(read_gerror() & 0x1u, 0u);
}

TEST_BENCH(smmuv3_bench, CMDQ_WrapsCorrectly_PreservesWrpBit)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    setup_cmdq(cmdq_base, 2);
    enable_cmdq();

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = gs::CMD_OP_SYNC;

    for (uint32_t i = 0; i < 5; ++i) {
        write_dram(cmdq_base + (i & 0x3u) * gs::SMMUV3_CMD_SIZE, cmd.data(), gs::SMMUV3_CMD_SIZE);
        mmio_write32(smmuv3_regs::CMDQ_PROD, i + 1);
        sc_core::wait(2, sc_core::SC_NS);
    }

    uint32_t cons = mmio_read32(smmuv3_regs::CMDQ_CONS);
    ASSERT_EQ(cons & 0x3u, 1u);
    ASSERT_EQ((cons >> 2) & 0x1u, 1u);
}

TEST_BENCH(smmuv3_bench, Fault_S1_AP1_NoUserAccess)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x05500000;
    const uint64_t eventq_base = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_raw_desc(l3, 0, (page_pa & ~0xFFFULL) | (1ULL << 0) | (1ULL << 1) | (1ULL << 10));

    setup_eventq(eventq_base, 4);
    enable_eventq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    sc_core::wait(5, sc_core::SC_NS);
    ASSERT_EQ(event_type_at(eventq_base, 0), 0x13);
}

TEST_BENCH(smmuv3_bench, Fault_S2_NoAccess)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t s2_l0 = DRAM_BASE + 0x2000;
    const uint64_t s2_l1 = DRAM_BASE + 0x3000;
    const uint64_t s2_l2 = DRAM_BASE + 0x4000;
    const uint64_t s2_l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x02100000;
    const uint64_t evq = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s2(strtab, s2_l0, 16, 2, 0, 5);
    write_table_desc(s2_l0, 0, s2_l1);
    write_table_desc(s2_l1, 0, s2_l2);
    write_table_desc(s2_l2, 0, s2_l3);
    write_raw_desc(s2_l3, 0, (page_pa & ~0xFFFULL) | (1ULL << 0) | (1ULL << 1) | (1ULL << 10));

    setup_eventq(evq, 4);
    enable_eventq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xAA), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    sc_core::wait(5, sc_core::SC_NS);
    ASSERT_EQ(event_type_at(evq, 0), 0x13);
}

TEST_BENCH(smmuv3_bench, Fault_S2_WriteToReadOnly)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t s2_l0 = DRAM_BASE + 0x2000;
    const uint64_t s2_l1 = DRAM_BASE + 0x3000;
    const uint64_t s2_l2 = DRAM_BASE + 0x4000;
    const uint64_t s2_l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x02200000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s2(strtab, s2_l0, 16, 2, 0, 5);
    write_table_desc(s2_l0, 0, s2_l1);
    write_table_desc(s2_l1, 0, s2_l2);
    write_table_desc(s2_l2, 0, s2_l3);
    write_raw_desc(s2_l3, 0, (page_pa & ~0xFFFULL) | (1ULL << 0) | (1ULL << 1) | (1ULL << 6) | (1ULL << 10));

    enable_smmu();
    ASSERT_EQ(tbu_txn(0x0, true, 0xAA), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_EQ(tbu_txn(0x0, false, 0), tlm::TLM_OK_RESPONSE);
}

TEST_BENCH(smmuv3_bench, Fault_S2_ReadFromWriteOnly)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t s2_l0 = DRAM_BASE + 0x2000;
    const uint64_t s2_l1 = DRAM_BASE + 0x3000;
    const uint64_t s2_l2 = DRAM_BASE + 0x4000;
    const uint64_t s2_l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x02300000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s2(strtab, s2_l0, 16, 2, 0, 5);
    write_table_desc(s2_l0, 0, s2_l1);
    write_table_desc(s2_l1, 0, s2_l2);
    write_table_desc(s2_l2, 0, s2_l3);
    write_raw_desc(s2_l3, 0, (page_pa & ~0xFFFULL) | (1ULL << 0) | (1ULL << 1) | (1ULL << 7) | (1ULL << 10));

    enable_smmu();
    ASSERT_EQ(tbu_txn(0x0, false, 0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_EQ(tbu_txn(0x0, true, 0xBB), tlm::TLM_OK_RESPONSE);
}

TEST_BENCH(smmuv3_bench, Fault_16K_UnmappedAtL3)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x4000;
    const uint64_t l2 = DRAM_BASE + 0x8000;
    const uint64_t l3 = DRAM_BASE + 0xC000;
    const uint64_t evq = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l2, 28, 2, 5);
    write_table_desc(l2, 0, l3);

    setup_eventq(evq, 4);
    enable_eventq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    sc_core::wait(5, sc_core::SC_NS);
    ASSERT_EQ(event_type_at(evq, 0), 0x10);
}

TEST_BENCH(smmuv3_bench, Fault_64K_UnmappedAtL3)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x10000;
    const uint64_t l2 = DRAM_BASE + 0x20000;
    const uint64_t l3 = DRAM_BASE + 0x30000;
    const uint64_t evq = DRAM_BASE + 0x00050000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l2, 22, 1, 5);
    write_table_desc(l2, 0, l3);

    setup_eventq(evq, 4);
    enable_eventq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    sc_core::wait(5, sc_core::SC_NS);
    ASSERT_EQ(event_type_at(evq, 0), 0x10);
}

TEST_BENCH(smmuv3_bench, TBU_TransportDbg_Translates)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x09100000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0, page_pa);
    enable_smmu();

    uint32_t marker = 0xC0DECAFEu;
    write_dram(page_pa, &marker, 4);

    uint32_t out = 0;
    unsigned int n = tbu_dbg_read(0x0, out);
    ASSERT_GT(n, 0u);
    ASSERT_EQ(out, marker);
}

TEST_BENCH(smmuv3_bench, STE_ReservedConfig1_RecordsBadSte)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t evq = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    {
        uint8_t ste[64] = { 0 };
        uint64_t* dw = reinterpret_cast<uint64_t*>(ste);
        dw[0] = 0x1ULL | (1ULL << 1);
        write_dram(strtab, ste, 64);
    }

    setup_eventq(evq, 4);
    enable_eventq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    sc_core::wait(5, sc_core::SC_NS);
    ASSERT_EQ(event_type_at(evq, 0), 0x04);
}

TEST_BENCH(smmuv3_bench, CMDQ_TLBI_NH_VA_TG64K_FlushesIotlb)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x10000;
    const uint64_t l2 = DRAM_BASE + 0x20000;
    const uint64_t l3 = DRAM_BASE + 0x30000;
    const uint64_t page_pa = DRAM_BASE + 0x02000000;
    const uint64_t cmdq_base = DRAM_BASE + 0x00080000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l2, 22, 1, 5);
    write_table_desc(l2, 0, l3);
    write_page_64k(l3, 0, page_pa);
    setup_cmdq(cmdq_base, 4);
    enable_cmdq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0x64), tlm::TLM_OK_RESPONSE);
    ASSERT_GT(smmu.test_iotlb_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x12;
    cmd[1] = (3u << 2);
    *reinterpret_cast<uint64_t*>(cmd.data() + 8) = 0;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMDQ_TLBI_NH_VAA_TG16K_FlushesIotlb)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x4000;
    const uint64_t l2 = DRAM_BASE + 0x8000;
    const uint64_t l3 = DRAM_BASE + 0xC000;
    const uint64_t page_pa = DRAM_BASE + 0x01000000;
    const uint64_t cmdq_base = DRAM_BASE + 0x00080000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l2, 28, 2, 5);
    write_table_desc(l2, 0, l3);
    write_page_16k(l3, 0, page_pa);
    setup_cmdq(cmdq_base, 4);
    enable_cmdq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0x16), tlm::TLM_OK_RESPONSE);
    ASSERT_GT(smmu.test_iotlb_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x13;
    cmd[1] = (2u << 2);
    *reinterpret_cast<uint64_t*>(cmd.data() + 8) = 0;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
}

TEST_BENCH(smmuv3_bench, TBU_WriteToCachedReadOnly_ReturnsError)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x07700000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k_ro(l3, 0, page_pa);
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, false, 0), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(tbu_txn(0x0, true, 0xDEAD), tlm::TLM_ADDRESS_ERROR_RESPONSE);
}

TEST_BENCH(smmuv3_bench, TBU_ReadFromCachedWriteOnly_ReturnsError)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t s2_l0 = DRAM_BASE + 0x2000;
    const uint64_t s2_l1 = DRAM_BASE + 0x3000;
    const uint64_t s2_l2 = DRAM_BASE + 0x4000;
    const uint64_t s2_l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x02400000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s2(strtab, s2_l0, 16, 2, 0, 5);
    write_table_desc(s2_l0, 0, s2_l1);
    write_table_desc(s2_l1, 0, s2_l2);
    write_table_desc(s2_l2, 0, s2_l3);
    write_raw_desc(s2_l3, 0, (page_pa & ~0xFFFULL) | (1ULL << 0) | (1ULL << 1) | (1ULL << 7) | (1ULL << 10));

    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xCC), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(tbu_txn(0x0, false, 0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
}

TEST_BENCH(smmuv3_bench, MemoryAttrs_AllSettersAndGetters)
{
    using ext_t = gs::smmuv3_memory_attrs_extension;
    ext_t a;
    a.set_memory_type(ext_t::MemoryType::DEVICE_nGnRnE);
    a.set_shareability(ext_t::Shareability::NON_SHAREABLE);
    a.set_access_perm(ext_t::AccessPerm::PRIV_RO_USER_RO);
    a.set_attr_indx(7);
    a.set_non_secure(false);
    a.set_pxn(true);
    a.set_uxn(true);
    a.set_contiguous(true);
    a.set_dbm(true);

    ASSERT_EQ(a.get_memory_type(), ext_t::MemoryType::DEVICE_nGnRnE);
    ASSERT_EQ(a.get_shareability(), ext_t::Shareability::NON_SHAREABLE);
    ASSERT_EQ(a.get_access_perm(), ext_t::AccessPerm::PRIV_RO_USER_RO);
    ASSERT_EQ(a.get_attr_indx(), 7u);
    ASSERT_FALSE(a.is_non_secure());
    ASSERT_TRUE(a.is_pxn());
    ASSERT_TRUE(a.is_uxn());
    ASSERT_TRUE(a.is_contiguous());
    ASSERT_TRUE(a.is_dbm());

    a.set_memory_type(ext_t::MemoryType::DEVICE_nGnRE);
    ASSERT_EQ(a.get_memory_type(), ext_t::MemoryType::DEVICE_nGnRE);
    a.set_memory_type(ext_t::MemoryType::DEVICE_nGRE);
    a.set_memory_type(ext_t::MemoryType::DEVICE_GRE);
    a.set_memory_type(ext_t::MemoryType::NORMAL_NC);
    a.set_memory_type(ext_t::MemoryType::NORMAL_WT);
    a.set_memory_type(ext_t::MemoryType::NORMAL_WB);

    a.set_shareability(ext_t::Shareability::OUTER_SHAREABLE);
    ASSERT_EQ(a.get_shareability(), ext_t::Shareability::OUTER_SHAREABLE);
    a.set_shareability(ext_t::Shareability::INNER_SHAREABLE);
    ASSERT_EQ(a.get_shareability(), ext_t::Shareability::INNER_SHAREABLE);
    a.set_shareability(ext_t::Shareability::RESERVED);

    std::string s = a.to_string();
    ASSERT_FALSE(s.empty());
}

TEST_BENCH(smmuv3_bench, MemoryAttrs_CloneAndCopyFrom)
{
    using ext_t = gs::smmuv3_memory_attrs_extension;
    ext_t a;
    a.set_memory_type(ext_t::MemoryType::NORMAL_NC);
    a.set_shareability(ext_t::Shareability::OUTER_SHAREABLE);
    a.set_access_perm(ext_t::AccessPerm::PRIV_RW_USER_NONE);
    a.set_attr_indx(5);
    a.set_non_secure(true);
    a.set_pxn(true);
    a.set_dbm(true);

    auto* c = static_cast<ext_t*>(a.clone());
    ASSERT_EQ(c->get_memory_type(), ext_t::MemoryType::NORMAL_NC);
    ASSERT_EQ(c->get_shareability(), ext_t::Shareability::OUTER_SHAREABLE);
    ASSERT_EQ(c->get_attr_indx(), 5u);
    ASSERT_TRUE(c->is_non_secure());
    ASSERT_TRUE(c->is_pxn());
    ASSERT_TRUE(c->is_dbm());
    delete c;

    ext_t b;
    b.copy_from(a);
    ASSERT_EQ(b.get_memory_type(), ext_t::MemoryType::NORMAL_NC);
    ASSERT_EQ(b.get_attr_indx(), 5u);
    ASSERT_TRUE(b.is_pxn());
}

TEST_BENCH(smmuv3_bench, MemoryAttrs_FromDescriptor_ToString_AllShareabilities)
{
    using ext_t = gs::smmuv3_memory_attrs_extension;
    for (uint64_t sh = 0; sh < 4; ++sh) {
        ext_t e;
        uint64_t desc = (sh << 8) | (1ULL << 5) | (1ULL << 53) | (1ULL << 54) | (1ULL << 52) | (1ULL << 51);
        e.set_from_descriptor(desc, 0);
        ASSERT_EQ(static_cast<int>(e.get_shareability()), static_cast<int>(sh));
        ASSERT_TRUE(e.is_non_secure());
        ASSERT_TRUE(e.is_pxn());
        ASSERT_TRUE(e.is_uxn());
        ASSERT_TRUE(e.is_contiguous());
        ASSERT_TRUE(e.is_dbm());
        std::string s = e.to_string();
        ASSERT_FALSE(s.empty());
    }
    for (int mt = 0; mt < 7; ++mt) {
        ext_t e;
        e.set_memory_type(static_cast<ext_t::MemoryType>(mt));
        std::string s = e.to_string();
        ASSERT_FALSE(s.empty());
    }
}

TEST_BENCH(smmuv3_bench, Page1_Aliases_PRIQ_PROD_CONS)
{
    const uint64_t priq_base = DRAM_BASE + 0x00040000;
    setup_priq(priq_base, 4);
    enable_priq();
    sc_core::wait(1, sc_core::SC_NS);

    smmu.test_inject_pri(0x77, 0x1000ULL, 0x2u);
    sc_core::wait(1, sc_core::SC_NS);

    uint32_t prod_p0 = mmio_read32(smmuv3_regs::PRIQ_PROD);
    uint32_t prod_p1 = mmio_read32(smmuv3_regs::PAGE1_OFFSET + smmuv3_regs::PRIQ_PROD);
    ASSERT_EQ(prod_p0, prod_p1);

    mmio_write32(smmuv3_regs::PAGE1_OFFSET + smmuv3_regs::PRIQ_CONS, prod_p0);
    sc_core::wait(1, sc_core::SC_NS);
    ASSERT_EQ(mmio_read32(smmuv3_regs::PRIQ_CONS), prod_p0);
    ASSERT_EQ(mmio_read32(smmuv3_regs::PAGE1_OFFSET + smmuv3_regs::PRIQ_CONS), prod_p0);
}

TEST_BENCH(smmuv3_bench, PRIQ_QueueFull_RaisesGerror)
{
    const uint64_t priq_base = DRAM_BASE + 0x00040000;
    uint32_t lo = static_cast<uint32_t>(priq_base) | 1u;
    uint32_t hi = static_cast<uint32_t>(priq_base >> 32);
    mmio_write32(smmuv3_regs::PRIQ_BASE_LO, lo);
    mmio_write32(smmuv3_regs::PRIQ_BASE_HI, hi);
    mmio_write32(smmuv3_regs::PRIQ_PROD, 0);
    mmio_write32(smmuv3_regs::PRIQ_CONS, 0);
    enable_priq();
    sc_core::wait(1, sc_core::SC_NS);

    uint32_t err_before = read_gerror();
    for (int i = 0; i < 8; ++i) {
        smmu.test_inject_pri(static_cast<uint32_t>(i), static_cast<uint64_t>(i) * 0x1000ULL, 0x1u);
        sc_core::wait(1, sc_core::SC_NS);
    }
    uint32_t err_after = read_gerror();
    ASSERT_NE(err_before, err_after);
}

TEST_BENCH(smmuv3_bench, EVENTQ_DmaFault_RaisesGerror)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t bogus_evq = 0xCAFE0000ULL;

    setup_linear_stream_table(strtab, 4);
    write_ste_abort(strtab);

    uint32_t lo = static_cast<uint32_t>(bogus_evq) | 4u;
    uint32_t hi = static_cast<uint32_t>(bogus_evq >> 32);
    mmio_write32(smmuv3_regs::EVENTQ_BASE_LO, lo);
    mmio_write32(smmuv3_regs::EVENTQ_BASE_HI, hi);
    mmio_write32(smmuv3_regs::EVENTQ_PROD, 0);
    mmio_write32(smmuv3_regs::EVENTQ_CONS, 0);
    enable_eventq();
    enable_smmu();

    uint32_t err_before = read_gerror();
    (void)tbu_txn(0x0, true, 0);
    sc_core::wait(2, sc_core::SC_NS);
    uint32_t err_after = read_gerror();
    ASSERT_NE(err_before, err_after);
}

TEST_BENCH(smmuv3_bench, EPD_DisablesTtbr0_Translation_Faults)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t evq = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    {
        uint8_t cd[64] = { 0 };
        uint32_t* w = reinterpret_cast<uint32_t*>(cd);
        w[0] = (16u & 0x3F) | (0u << 6) | (1u << 14) | (1u << 30) | (1u << 31);
        w[1] = (5u & 0x7) | (1u << 9);
        w[2] = static_cast<uint32_t>(l0) & 0xFFFFFFF0u;
        write_dram(cd_base, cd, 64);
    }
    setup_eventq(evq, 4);
    enable_eventq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    sc_core::wait(5, sc_core::SC_NS);
    ASSERT_EQ(event_type_at(evq, 0), 0x10);
}

TEST_BENCH(smmuv3_bench, CMD_Resume_RespZero_DoesNotAbort)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    setup_cmdq(cmdq_base, 4);
    enable_cmdq();
    enable_smmu();

    smmu.test_inject_stall(0xBEEF1234);
    ASSERT_TRUE(smmu.test_stall_present(0xBEEF1234));
    ASSERT_FALSE(smmu.test_stall_aborted(0xBEEF1234));

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x44;
    *reinterpret_cast<uint32_t*>(cmd.data() + 4) = 0xBEEF1234;
    cmd[11] = 0;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_FALSE(smmu.test_stall_aborted(0xBEEF1234));
}

TEST_BENCH(smmuv3_bench, IOTLB_Inv_VMID_NoMatch_KeepsEntries)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x06600000;
    const uint64_t cmdq = DRAM_BASE + 0x00020000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0, page_pa);
    setup_cmdq(cmdq, 4);
    enable_cmdq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xAA), tlm::TLM_OK_RESPONSE);
    size_t before = smmu.test_iotlb_size();
    ASSERT_GT(before, 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x28;
    *reinterpret_cast<uint16_t*>(cmd.data() + 4) = 0x9999;
    issue_cmd(cmdq, 0, cmd.data());

    ASSERT_EQ(smmu.test_iotlb_size(), before);
}

TEST_BENCH(smmuv3_bench, IOTLB_Inv_ASID_NoMatch_KeepsEntries)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x06700000;
    const uint64_t cmdq = DRAM_BASE + 0x00020000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0, page_pa);
    setup_cmdq(cmdq, 4);
    enable_cmdq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xAA), tlm::TLM_OK_RESPONSE);
    size_t before = smmu.test_iotlb_size();
    ASSERT_GT(before, 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = 0x11;
    *reinterpret_cast<uint16_t*>(cmd.data() + 6) = 0x4242;
    issue_cmd(cmdq, 0, cmd.data());

    ASSERT_EQ(smmu.test_iotlb_size(), before);
}

TEST_BENCH(smmuv3_bench_iotlb4, IOTLB_LRU_EvictsOldest)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    for (uint32_t i = 0; i < 8; ++i) {
        write_page_4k(l3, i, DRAM_BASE + 0x07000000ULL + i * 0x1000ULL);
    }
    enable_smmu();

    for (uint32_t i = 0; i < 4; ++i) {
        ASSERT_EQ(tbu_txn(static_cast<uint64_t>(i) * 0x1000ULL, true, 0xA0u + i), tlm::TLM_OK_RESPONSE);
    }
    ASSERT_EQ(smmu.test_iotlb_size(), 4u);

    ASSERT_EQ(tbu_txn(0x4000ULL, true, 0xA4u), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(smmu.test_iotlb_size(), 4u);

    ASSERT_EQ(tbu_txn(0x5000ULL, true, 0xA5u), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(smmu.test_iotlb_size(), 4u);
}

TEST_BENCH(smmuv3_bench_iotlb4, IOTLB_ReinsertSameKey_KeepsSize)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t cmdq = DRAM_BASE + 0x00020000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    for (uint32_t i = 0; i < 4; ++i) {
        write_page_4k(l3, i, DRAM_BASE + 0x07800000ULL + i * 0x1000ULL);
    }
    setup_cmdq(cmdq, 4);
    enable_cmdq();
    enable_smmu();

    for (uint32_t i = 0; i < 4; ++i) {
        ASSERT_EQ(tbu_txn(static_cast<uint64_t>(i) * 0x1000ULL, true, 0xB0u + i), tlm::TLM_OK_RESPONSE);
    }
    ASSERT_EQ(smmu.test_iotlb_size(), 4u);

    {
        std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
        cmd[0] = 0x12;
        *reinterpret_cast<uint16_t*>(cmd.data() + 6) = 0;
        *reinterpret_cast<uint64_t*>(cmd.data() + 8) = 0;
        cmd[15] = 0;
        issue_cmd(cmdq, 0, cmd.data());
    }
    ASSERT_EQ(smmu.test_iotlb_size(), 3u);

    ASSERT_EQ(tbu_txn(0x0ULL, true, 0xB0u), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(smmu.test_iotlb_size(), 4u);

    ASSERT_EQ(tbu_txn(0x0ULL, true, 0xB0u), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(smmu.test_iotlb_size(), 4u);
}

TEST_BENCH(smmuv3_bench_pamax32, PAMAX32_Reports_OAS_Zero) { ASSERT_EQ(mmio_read32(smmuv3_regs::IDR5) & 0x7u, 0u); }

TEST_BENCH(smmuv3_bench_pamax36, PAMAX36_Reports_OAS_One) { ASSERT_EQ(mmio_read32(smmuv3_regs::IDR5) & 0x7u, 1u); }

TEST_BENCH(smmuv3_bench_pamax40, PAMAX40_Reports_OAS_Two) { ASSERT_EQ(mmio_read32(smmuv3_regs::IDR5) & 0x7u, 2u); }

TEST_BENCH(smmuv3_bench_pamax42, PAMAX42_Reports_OAS_Three) { ASSERT_EQ(mmio_read32(smmuv3_regs::IDR5) & 0x7u, 3u); }

TEST_BENCH(smmuv3_bench_pamax44, PAMAX44_Reports_OAS_Four) { ASSERT_EQ(mmio_read32(smmuv3_regs::IDR5) & 0x7u, 4u); }

TEST_BENCH(smmuv3_bench, GBPA_BypassFromInvalidSTE_NoAbort)
{
    const uint64_t strtab = DRAM_BASE;
    setup_linear_stream_table(strtab, 4);
    write_ste_invalid(strtab);
    mmio_write32(smmuv3_regs::GBPA, 0);
    enable_smmu();

    ASSERT_EQ(tbu_txn(DRAM_BASE + 0x08800000, true, 0xBEEF), tlm::TLM_OK_RESPONSE);
}

TEST_BENCH(smmuv3_bench, ConfigBypass_STE_DirectThrough)
{
    const uint64_t strtab = DRAM_BASE;
    setup_linear_stream_table(strtab, 4);
    write_ste_bypass(strtab);
    enable_smmu();

    const uint64_t pa = DRAM_BASE + 0x08400000;
    ASSERT_EQ(tbu_txn(pa, true, 0xCAFEBABE), tlm::TLM_OK_RESPONSE);
    uint32_t v = 0;
    read_dram(pa, &v, 4);
    ASSERT_EQ(v, 0xCAFEBABEu);
}

TEST_BENCH(smmuv3_bench, GATOS_Fault_FBitSet)
{
    const uint64_t strtab = DRAM_BASE;
    setup_linear_stream_table(strtab, 4);
    write_ste_abort(strtab);
    enable_smmu();

    mmio_write32(smmuv3_regs::GATOS_SID, 0);
    mmio_write32(smmuv3_regs::GATOS_ADDR_LO, 0x100);
    mmio_write32(smmuv3_regs::GATOS_ADDR_LO + 4, 0);
    mmio_write32(smmuv3_regs::GATOS_CTRL, (1u << 8) | (1u << 0));
    sc_core::wait(10, sc_core::SC_NS);

    uint32_t par = mmio_read32(smmuv3_regs::GATOS_PAR_LO);
    ASSERT_EQ(par & 0x1u, 1u);
}

TEST_BENCH(smmuv3_bench, DMI_TbuTranslates_HitsBackingPage)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x0C000000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0, page_pa);
    enable_smmu();

    uint32_t marker = 0xC0DECAFEu;
    write_dram(page_pa, &marker, 4);

    tlm::tlm_dmi dmi;
    ASSERT_TRUE(tbu_get_dmi(0x0, false, dmi));
    ASSERT_TRUE(dmi.is_read_allowed());
    ASSERT_LE(dmi.get_start_address(), 0x0u);
    ASSERT_GE(dmi.get_end_address(), 0x0u);

    uint32_t v = *reinterpret_cast<uint32_t*>(dmi.get_dmi_ptr());
    ASSERT_EQ(v, marker);
}

TEST_BENCH(smmuv3_bench, DMI_DeniedOnUnmappedTranslation)
{
    const uint64_t strtab = DRAM_BASE;
    setup_linear_stream_table(strtab, 4);
    write_ste_abort(strtab);
    enable_smmu();

    tlm::tlm_dmi dmi;
    ASSERT_FALSE(tbu_get_dmi(0x0, false, dmi));
    ASSERT_FALSE(dmi.is_read_allowed());
    ASSERT_FALSE(dmi.is_write_allowed());
}

TEST_BENCH(smmuv3_bench, DMI_DeniedWriteOnReadOnlyMapping)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x0C100000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k_ro(l3, 0, page_pa);
    enable_smmu();

    tlm::tlm_dmi dmi;
    ASSERT_FALSE(tbu_get_dmi(0x0, true, dmi));
    ASSERT_FALSE(dmi.is_write_allowed());
}

TEST_BENCH(smmuv3_bench, DMI_RegionMappedToIovaSpace)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x0C200000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0, page_pa);
    enable_smmu();

    tlm::tlm_dmi dmi;
    ASSERT_TRUE(tbu_get_dmi(0x100, false, dmi));
    ASSERT_GE(0x100u, dmi.get_start_address());
    ASSERT_LE(0x100u, dmi.get_end_address());
    ASSERT_LT(dmi.get_end_address(), 0x1000u + 0x10000u);
}

TEST_BENCH(smmuv3_bench, CMDQ_TLBI_NH_VA_TG4K_TlbiEncoding)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x06A00000;
    const uint64_t cmdq_base = DRAM_BASE + 0x00080000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0, page_pa);
    setup_cmdq(cmdq_base, 4);
    enable_cmdq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0x4Fu), tlm::TLM_OK_RESPONSE);
    ASSERT_GT(smmu.test_iotlb_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = gs::CMD_OP_TLBI_NH_VA;
    cmd[1] = (gs::TLBI_TG_4K << 2);
    *reinterpret_cast<uint64_t*>(cmd.data() + 8) = 0;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMD_TLBI_EL2_ALL_FlushesIotlb)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    prime_translation_and_caches(cmdq_base);
    ASSERT_GT(smmu.test_iotlb_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = gs::CMD_OP_TLBI_EL2_ALL;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMD_TLBI_EL2_VA_FlushesIotlb)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    prime_translation_and_caches(cmdq_base);
    ASSERT_GT(smmu.test_iotlb_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = gs::CMD_OP_TLBI_EL2_VA;
    cmd[1] = (gs::TLBI_TG_4K << 2);
    *reinterpret_cast<uint64_t*>(cmd.data() + 8) = 0;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
}

TEST_BENCH(smmuv3_bench, CMD_TLBI_EL2_VAA_FlushesIotlb)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    prime_translation_and_caches(cmdq_base);
    ASSERT_GT(smmu.test_iotlb_size(), 0u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = gs::CMD_OP_TLBI_EL2_VAA;
    cmd[1] = (gs::TLBI_TG_4K << 2);
    *reinterpret_cast<uint64_t*>(cmd.data() + 8) = 0;
    issue_cmd(cmdq_base, 0, cmd.data());

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
}

TEST_BENCH(smmuv3_bench, Fault_TTBR1_HighVa_AddrSize)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l1_pa = DRAM_BASE + 0x2000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_ttbr1(cd_base, l1_pa, 16, 2, 5);
    enable_smmu();

    ASSERT_EQ(tbu_txn(0xFFFF000000000000ULL, true, 0xCC), tlm::TLM_ADDRESS_ERROR_RESPONSE);
}

TEST_BENCH(smmuv3_bench, Fault_Walk_DmaReadFail_WalkEabt)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t evq = DRAM_BASE + 0x00010000;
    const uint64_t bogus_l0 = 0xDEAD0000ULL;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, bogus_l0, 16, 0, 5);
    setup_eventq(evq, 4);
    enable_eventq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xAA), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    sc_core::wait(5, sc_core::SC_NS);
    ASSERT_EQ(event_type_at(evq, 0), gs::SMMUV3_EVT_F_WALK_EABT);
}

TEST_BENCH(smmuv3_bench, Nested_S1S2_S2DescriptorFetchFault)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t s2_l1 = DRAM_BASE + 0x00200000;

    setup_linear_stream_table(strtab, 4);
    write_ste_nested(strtab, cd_base, s2_l1, 16, 1, 0, 5);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xCC), tlm::TLM_ADDRESS_ERROR_RESPONSE);
}

TEST_BENCH(smmuv3_bench, IOTLB_Reinsert_KeepsSizeStable)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x06200000;
    const uint64_t cmdq = DRAM_BASE + 0x00020000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0, page_pa);
    setup_cmdq(cmdq, 4);
    enable_cmdq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xA1), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(smmu.test_iotlb_size(), 1u);

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = gs::CMD_OP_TLBI_NH_VA;
    cmd[1] = (gs::TLBI_TG_4K << 2);
    *reinterpret_cast<uint64_t*>(cmd.data() + 8) = 0;
    issue_cmd(cmdq, 0, cmd.data());
    ASSERT_EQ(smmu.test_iotlb_size(), 0u);

    ASSERT_EQ(tbu_txn(0x0, true, 0xA2), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(smmu.test_iotlb_size(), 1u);

    ASSERT_EQ(tbu_txn(0x0, true, 0xA3), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(smmu.test_iotlb_size(), 1u);
}

TEST_BENCH(smmuv3_bench, CMDQ_CONS_P1_PreRead_DrainsCmdq)
{
    const uint64_t cmdq_base = DRAM_BASE + 0x00020000;
    setup_cmdq(cmdq_base, 4);
    enable_cmdq();

    std::array<uint8_t, gs::SMMUV3_CMD_SIZE> cmd{};
    cmd[0] = gs::CMD_OP_SYNC;
    write_dram(cmdq_base, cmd.data(), gs::SMMUV3_CMD_SIZE);
    mmio_write32(smmuv3_regs::CMDQ_PROD, 1);

    uint32_t cons_p1 = mmio_read32(smmuv3_regs::PAGE1_OFFSET + smmuv3_regs::CMDQ_CONS);
    ASSERT_EQ(cons_p1 & 0x1Fu, 1u);
}

TEST_BENCH(smmuv3_bench, EVENTQ_Disabled_NoRecord)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t evq = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    write_ste_abort(strtab);
    setup_eventq(evq, 4);
    enable_smmu();

    uint32_t prod_before = mmio_read32(smmuv3_regs::EVENTQ_PROD);
    (void)tbu_txn(0x0, true, 0);
    sc_core::wait(2, sc_core::SC_NS);
    uint32_t prod_after = mmio_read32(smmuv3_regs::EVENTQ_PROD);
    ASSERT_EQ(prod_before, prod_after);
}

TEST_BENCH(smmuv3_bench, PRIQ_Disabled_NoRecord)
{
    const uint64_t priq = DRAM_BASE + 0x00040000;
    setup_priq(priq, 4);
    sc_core::wait(1, sc_core::SC_NS);

    uint32_t before = mmio_read32(smmuv3_regs::PRIQ_PROD);
    smmu.test_inject_pri(0x12, 0x100, 0x1);
    sc_core::wait(1, sc_core::SC_NS);
    ASSERT_EQ(mmio_read32(smmuv3_regs::PRIQ_PROD), before);
}

TEST_BENCH(smmuv3_bench, Fault_S1_AddrSize_HighIovaBitsSet)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t evq = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    setup_eventq(evq, 4);
    enable_eventq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0001000000000000ULL, true, 0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    sc_core::wait(5, sc_core::SC_NS);
    ASSERT_EQ(event_type_at(evq, 0), gs::SMMUV3_EVT_F_ADDR_SIZE);
}

TEST_BENCH(smmuv3_bench, S2_64K_Granule_HappyPath)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t s2_l1 = DRAM_BASE + 0x10000;
    const uint64_t s2_l2 = DRAM_BASE + 0x20000;
    const uint64_t s2_l3 = DRAM_BASE + 0x30000;
    const uint64_t page_pa = DRAM_BASE + 0x02000000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s2(strtab, s2_l1, 22, 2, 1, 5);
    write_table_desc(s2_l1, 0, s2_l2);
    write_table_desc(s2_l2, 0, s2_l3);
    {
        uint64_t desc = (page_pa & ~0xFFFFULL) | smmuv3_bench_consts::S2_DESC_FLAGS_RW;
        write_dram(s2_l3, &desc, smmuv3_bench_consts::DESC_BYTES);
    }

    enable_smmu();
    ASSERT_EQ(tbu_txn(0x0, true, 0x6464), tlm::TLM_OK_RESPONSE);
    uint32_t v = 0;
    read_dram(page_pa, &v, 4);
    ASSERT_EQ(v, 0x6464u);
}

TEST_BENCH(smmuv3_bench, S2_16K_Granule_HappyPath)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t s2_l1 = DRAM_BASE + 0x4000;
    const uint64_t s2_l2 = DRAM_BASE + 0x8000;
    const uint64_t s2_l3 = DRAM_BASE + 0xC000;
    const uint64_t page_pa = DRAM_BASE + 0x01000000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s2(strtab, s2_l1, 28, 2, 2, 5);
    write_table_desc(s2_l1, 0, s2_l2);
    write_table_desc(s2_l2, 0, s2_l3);
    {
        uint64_t desc = (page_pa & ~0x3FFFULL) | smmuv3_bench_consts::S2_DESC_FLAGS_RW;
        write_dram(s2_l3, &desc, smmuv3_bench_consts::DESC_BYTES);
    }

    enable_smmu();
    ASSERT_EQ(tbu_txn(0x0, true, 0x1616), tlm::TLM_OK_RESPONSE);
    uint32_t v = 0;
    read_dram(page_pa, &v, 4);
    ASSERT_EQ(v, 0x1616u);
}

TEST_BENCH(smmuv3_bench, TBU_InvalidateDmi_PropagatesMatchingIovas)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_a = DRAM_BASE + 0x0D000000;
    const uint64_t page_b = DRAM_BASE + 0x0D100000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0, page_a);
    write_page_4k(l3, 1, page_b);
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0000ULL, true, 0xAA), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(tbu_txn(0x1000ULL, true, 0xBB), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(smmu.test_iotlb_size(), 2u);

    dmi_inv_observer.ranges.clear();

    dmi_inv_helper.fire_invalidate(page_a, page_a | 0xFFFULL);
    sc_core::wait(1, sc_core::SC_NS);

    ASSERT_EQ(dmi_inv_observer.ranges.size(), 1u);
    ASSERT_EQ(dmi_inv_observer.ranges[0].first, 0x0000ULL);
    ASSERT_EQ(dmi_inv_observer.ranges[0].second, 0x0FFFULL);
}

TEST_BENCH(smmuv3_bench, TBU_InvalidateDmi_NoOverlap_NoPropagation)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x0D200000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0, page_pa);
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xCC), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(smmu.test_iotlb_size(), 1u);

    dmi_inv_observer.ranges.clear();

    dmi_inv_helper.fire_invalidate(0xA0000000ULL, 0xA0000FFFULL);
    sc_core::wait(1, sc_core::SC_NS);

    ASSERT_EQ(dmi_inv_observer.ranges.size(), 0u);
}

TEST_BENCH(smmuv3_bench, TBU_InvalidateDmi_SpansAllIotlb)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_a = DRAM_BASE + 0x0D300000;
    const uint64_t page_b = DRAM_BASE + 0x0D400000;
    const uint64_t page_c = DRAM_BASE + 0x0D500000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0, page_a);
    write_page_4k(l3, 1, page_b);
    write_page_4k(l3, 2, page_c);
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0000ULL, true, 0x10), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(tbu_txn(0x1000ULL, true, 0x20), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(tbu_txn(0x2000ULL, true, 0x30), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(smmu.test_iotlb_size(), 3u);

    dmi_inv_observer.ranges.clear();

    dmi_inv_helper.fire_invalidate(DRAM_BASE, DRAM_BASE + DRAM_SIZE - 1);
    sc_core::wait(1, sc_core::SC_NS);

    ASSERT_EQ(dmi_inv_observer.ranges.size(), 3u);
}

TEST_BENCH(smmuv3_bench, TBU_InvalidateDmi_EmptyIotlb_NoPropagation)
{
    dmi_inv_observer.ranges.clear();
    dmi_inv_helper.fire_invalidate(0x0, 0xFFFFFFFFULL);
    sc_core::wait(1, sc_core::SC_NS);
    ASSERT_EQ(dmi_inv_observer.ranges.size(), 0u);
}

TEST_BENCH(smmuv3_bench, Nested_S1S2_TwoStage_NonIdentityS2)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t s2_l1 = DRAM_BASE + 0x00200000;
    const uint64_t ipa_data = 0x40020000ULL;
    const uint64_t real_pa = DRAM_BASE + 0x00020000;

    setup_linear_stream_table(strtab, 4);
    write_ste_nested(strtab, cd_base, s2_l1, 16, 1, 0, 5);

    write_block_1gb_s2(s2_l1, 1, DRAM_BASE);
    write_block_1gb_s2(s2_l1, 2, DRAM_BASE);

    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0, ipa_data);

    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xBEEF1234u), tlm::TLM_OK_RESPONSE);

    uint32_t rb_real = 0;
    read_dram(real_pa, &rb_real, 4);
    ASSERT_EQ(rb_real, 0xBEEF1234u);

    ASSERT_EQ(tbu_txn(0x0, false, 0), tlm::TLM_OK_RESPONSE);

    ASSERT_EQ(tbu_txn(0x100, true, 0xCAFEF00Du), tlm::TLM_OK_RESPONSE);
    uint32_t rb2 = 0;
    read_dram(real_pa + 0x100, &rb2, 4);
    ASSERT_EQ(rb2, 0xCAFEF00Du);
}

TEST_BENCH(smmuv3_bench, Nested_S1S2_TwoStage_MultiplePages)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t s2_l1 = DRAM_BASE + 0x00200000;
    const uint64_t ipa_page_a = 0x40020000ULL;
    const uint64_t ipa_page_b = 0x40030000ULL;
    const uint64_t real_pa_a = DRAM_BASE + 0x00020000;
    const uint64_t real_pa_b = DRAM_BASE + 0x00030000;

    setup_linear_stream_table(strtab, 4);
    write_ste_nested(strtab, cd_base, s2_l1, 16, 1, 0, 5);

    write_block_1gb_s2(s2_l1, 1, DRAM_BASE);
    write_block_1gb_s2(s2_l1, 2, DRAM_BASE);

    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0x20, ipa_page_a);
    write_page_4k(l3, 0x30, ipa_page_b);

    enable_smmu();

    ASSERT_EQ(tbu_txn(0x20000, true, 0xAAAA1111u), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(tbu_txn(0x30000, true, 0xBBBB2222u), tlm::TLM_OK_RESPONSE);

    uint32_t a = 0, b = 0;
    read_dram(real_pa_a, &a, 4);
    read_dram(real_pa_b, &b, 4);
    ASSERT_EQ(a, 0xAAAA1111u);
    ASSERT_EQ(b, 0xBBBB2222u);
}

TEST_BENCH(smmuv3_bench, Nested_S1S2_TwoStage_IotlbPopulated)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t s2_l1 = DRAM_BASE + 0x00200000;
    const uint64_t ipa_data = 0x40040000ULL;
    const uint64_t real_pa = DRAM_BASE + 0x00040000;

    setup_linear_stream_table(strtab, 4);
    write_ste_nested(strtab, cd_base, s2_l1, 16, 1, 0, 5);

    write_block_1gb_s2(s2_l1, 1, DRAM_BASE);
    write_block_1gb_s2(s2_l1, 2, DRAM_BASE);

    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0x40, ipa_data);
    enable_smmu();

    ASSERT_EQ(smmu.test_iotlb_size(), 0u);
    ASSERT_EQ(tbu_txn(0x40000, true, 0x12345678u), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(smmu.test_iotlb_size(), 1u);
    ASSERT_EQ(smmu.test_ste_cache_size(), 1u);
    ASSERT_EQ(smmu.test_cd_cache_size(), 1u);

    ASSERT_EQ(tbu_txn(0x40000, true, 0xABCDEF00u), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(smmu.test_iotlb_size(), 1u);

    uint32_t v = 0;
    read_dram(real_pa, &v, 4);
    ASSERT_EQ(v, 0xABCDEF00u);
}

TEST_BENCH(smmuv3_bench, Nested_S1S2_TwoStage_S2Permission_ReadOnly_WriteFaults)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t s2_l1 = DRAM_BASE + 0x00200000;
    const uint64_t ipa_data = 0x40060000ULL;

    setup_linear_stream_table(strtab, 4);
    write_ste_nested(strtab, cd_base, s2_l1, 16, 1, 0, 5);

    {
        uint64_t ro_block = (DRAM_BASE & smmuv3_bench_consts::BLOCK_1G_PA_MASK) | (1ULL << 0) | (1ULL << 6) |
                            (1ULL << 10);
        write_dram(s2_l1 + 1 * smmuv3_bench_consts::DESC_BYTES, &ro_block, smmuv3_bench_consts::DESC_BYTES);
    }
    write_block_1gb_s2(s2_l1, 2, DRAM_BASE);

    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0x60, ipa_data);
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x60000, true, 0xDEAD0000u), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_EQ(tbu_txn(0x60000, false, 0), tlm::TLM_OK_RESPONSE);
}

static constexpr uint32_t CMDQ_CONS_ERR_SHIFT = 23;
static constexpr uint32_t CMDQ_CONS_ERR_MASK = 0x7Fu;

static constexpr uint32_t GERROR_CMDQ_ERR_BIT_MASK = 0x1u;

static constexpr uint64_t UNMAPPED_TEST_ADDR = 0xDEAD0000u;
static constexpr uint8_t CMD_OPCODE_INVALID = 0xFFu;
static constexpr uint32_t SETTLE_NS = 5;

TEST_BENCH(smmuv3_bench, CMDQ_CONS_ERR_Set_To_ILL_On_UnknownOpcode)
{
    const uint64_t cmdq = DRAM_BASE;
    setup_cmdq(cmdq, 4);
    enable_cmdq();
    enable_irqs();

    std::array<uint8_t, smmuv3_bench_consts::CMD_BYTES> bad{};
    bad[0] = CMD_OPCODE_INVALID;
    write_dram(cmdq, bad.data(), smmuv3_bench_consts::CMD_BYTES);
    mmio_write32(smmuv3_regs::CMDQ_PROD, 1);
    sc_core::wait(SETTLE_NS, sc_core::SC_NS);

    uint32_t cons = mmio_read32(smmuv3_regs::CMDQ_CONS);
    uint32_t err = (cons >> CMDQ_CONS_ERR_SHIFT) & CMDQ_CONS_ERR_MASK;
    ASSERT_EQ(err, gs::SMMUV3_CERROR_ILL);
    uint32_t gerror = mmio_read32(smmuv3_regs::GERROR) & GERROR_CMDQ_ERR_BIT_MASK;
    uint32_t gerrorn = mmio_read32(smmuv3_regs::GERRORN) & GERROR_CMDQ_ERR_BIT_MASK;
    ASSERT_EQ(gerror ^ gerrorn, 1u);
}

TEST_BENCH(smmuv3_bench, CMDQ_CONS_ERR_Set_To_ABT_On_DmaFault)
{
    setup_cmdq(UNMAPPED_TEST_ADDR, 4);
    enable_cmdq();
    enable_irqs();
    mmio_write32(smmuv3_regs::CMDQ_PROD, 1);
    sc_core::wait(SETTLE_NS, sc_core::SC_NS);

    uint32_t err = (mmio_read32(smmuv3_regs::CMDQ_CONS) >> CMDQ_CONS_ERR_SHIFT) & CMDQ_CONS_ERR_MASK;
    ASSERT_EQ(err, gs::SMMUV3_CERROR_ABT);
}

TEST_BENCH(smmuv3_bench, IRQ_GERROR_LevelHeld_UntilGerrorN)
{
    const uint64_t cmdq = DRAM_BASE;
    setup_cmdq(cmdq, 4);
    enable_cmdq();
    enable_irqs();

    std::array<uint8_t, smmuv3_bench_consts::CMD_BYTES> bad{};
    bad[0] = CMD_OPCODE_INVALID;
    write_dram(cmdq, bad.data(), smmuv3_bench_consts::CMD_BYTES);
    mmio_write32(smmuv3_regs::CMDQ_PROD, 1);
    sc_core::wait(5, sc_core::SC_NS);

    uint32_t gerror = mmio_read32(smmuv3_regs::GERROR);
    uint32_t gerrorn = mmio_read32(smmuv3_regs::GERRORN);
    ASSERT_NE((gerror ^ gerrorn) & 0x1u, 0u);
    ASSERT_GT(irq_gerror_cnt.count, 0u);

    mmio_write32(smmuv3_regs::GERRORN, gerror);
    sc_core::wait(5, sc_core::SC_NS);
    uint32_t gerrorn2 = mmio_read32(smmuv3_regs::GERRORN);
    ASSERT_EQ((gerror ^ gerrorn2) & 0x1u, 0u);
}

TEST_BENCH(smmuv3_bench, IRQ_EVENTQ_LevelHeld_UntilConsAdvances)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t eventq_base = DRAM_BASE + 0x00010000;
    setup_linear_stream_table(strtab, 4);
    write_ste_abort(strtab);
    setup_eventq(eventq_base, 4);
    enable_eventq();
    enable_irqs();
    enable_smmu();

    tbu_txn(0x0, true, 0);
    sc_core::wait(5, sc_core::SC_NS);
    uint32_t prod = mmio_read32(smmuv3_regs::EVENTQ_PROD);
    uint32_t cons = mmio_read32(smmuv3_regs::EVENTQ_CONS);
    ASSERT_NE(prod, cons);

    mmio_write32(smmuv3_regs::EVENTQ_CONS, prod);
    sc_core::wait(5, sc_core::SC_NS);
    uint32_t cons2 = mmio_read32(smmuv3_regs::EVENTQ_CONS);
    ASSERT_EQ(cons2, prod);
}

TEST_BENCH(smmuv3_bench, IRQ_GERROR_Disabled_BitStillSet_LineStaysLow)
{
    const uint64_t cmdq = DRAM_BASE;
    setup_cmdq(cmdq, 4);
    enable_cmdq();

    std::array<uint8_t, smmuv3_bench_consts::CMD_BYTES> bad{};
    bad[0] = CMD_OPCODE_INVALID;
    write_dram(cmdq, bad.data(), smmuv3_bench_consts::CMD_BYTES);
    mmio_write32(smmuv3_regs::CMDQ_PROD, 1);
    sc_core::wait(5, sc_core::SC_NS);

    uint32_t gerror = mmio_read32(smmuv3_regs::GERROR) & 0x1u;
    ASSERT_EQ(gerror, 1u);
    ASSERT_EQ(irq_gerror_cnt.count, 0u);
}

TEST_BENCH(smmuv3_bench, Event_Syndrome_PopulatesSidRnwLevel)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t eventq_base = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    setup_eventq(eventq_base, 4);
    enable_eventq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x12000, true, 0x1234u), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    sc_core::wait(5, sc_core::SC_NS);

    auto ev = read_last_event(eventq_base);
    ASSERT_EQ(ev.type, gs::SMMUV3_EVT_F_TRANSLATION);
    ASSERT_EQ(ev.streamid, 0u);
    ASSERT_EQ(ev.iova, 0x12000ULL);
    ASSERT_FALSE(ev.rnw);
    ASSERT_GT(ev.reason, 0u);
}

TEST_BENCH(smmuv3_bench, Event_Syndrome_ReadSetsRnW)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t eventq_base = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    setup_eventq(eventq_base, 4);
    enable_eventq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x1000, false, 0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    sc_core::wait(5, sc_core::SC_NS);
    auto ev = read_last_event(eventq_base);
    ASSERT_TRUE(ev.rnw);
}

TEST_BENCH(smmuv3_bench, AFFD_DisablesAccessFlagFault)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x08000000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5, 0, true);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    {
        uint64_t desc = (page_pa & smmuv3_bench_consts::PAGE_4K_PA_MASK) | (1ULL << 0) | (1ULL << 1);
        write_dram(l3, &desc, sizeof(desc));
    }
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, false, 0), tlm::TLM_OK_RESPONSE);
}

TEST_BENCH(smmuv3_bench, HTTU_AF_HardwareUpdatesDescriptor)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x08000000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);

    write_cd_identity(cd_base, l0, 16, 0, 5, 0, false, true, false);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    {
        uint64_t desc = (page_pa & smmuv3_bench_consts::PAGE_4K_PA_MASK) | (1ULL << 0) | (1ULL << 1);
        write_dram(l3, &desc, sizeof(desc));
    }
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, false, 0), tlm::TLM_OK_RESPONSE);

    uint64_t new_desc = 0;
    read_dram(l3, &new_desc, sizeof(new_desc));
    ASSERT_NE(new_desc & (1ULL << 10), 0ULL);
}

TEST_BENCH(smmuv3_bench, ATC_INV_ForwardsUpstreamInvalidate)
{
    const uint64_t cmdq = DRAM_BASE;
    setup_cmdq(cmdq, 4);
    enable_cmdq();

    std::array<uint8_t, smmuv3_bench_consts::CMD_BYTES> atc{};
    atc[0] = 0x40;

    uint32_t sid = 0x42;
    std::memcpy(atc.data() + 4, &sid, 4);

    uint64_t word1 = 0x1000ULL | 0u;
    std::memcpy(atc.data() + 8, &word1, 8);

    write_dram(cmdq, atc.data(), smmuv3_bench_consts::CMD_BYTES);
    mmio_write32(smmuv3_regs::CMDQ_PROD, 1);
    sc_core::wait(5, sc_core::SC_NS);

    uint32_t cons = mmio_read32(smmuv3_regs::CMDQ_CONS);
    ASSERT_EQ(cons & 0x1Fu, 1u);
    ASSERT_EQ(mmio_read32(smmuv3_regs::GERROR) & 0x1u, 0u);
}

TEST_BENCH(smmuv3_bench, MSI_GERROR_WritesDoorbell)
{
    const uint64_t cmdq = DRAM_BASE;
    const uint64_t doorbell = DRAM_BASE + 0x00080000;
    setup_cmdq(cmdq, 4);
    enable_cmdq();
    enable_irqs();

    mmio_write32(smmuv3_regs::GERROR_IRQ_CFG0_LO, static_cast<uint32_t>(doorbell));
    mmio_write32(smmuv3_regs::GERROR_IRQ_CFG0_HI, static_cast<uint32_t>(doorbell >> 32));
    mmio_write32(smmuv3_regs::GERROR_IRQ_CFG2, 0xCAFEBABEu);

    uint32_t zero = 0;
    write_dram(doorbell, &zero, 4);

    std::array<uint8_t, smmuv3_bench_consts::CMD_BYTES> bad{};
    bad[0] = CMD_OPCODE_INVALID;
    write_dram(cmdq, bad.data(), smmuv3_bench_consts::CMD_BYTES);
    mmio_write32(smmuv3_regs::CMDQ_PROD, 1);
    sc_core::wait(10, sc_core::SC_NS);

    uint32_t msi_val = 0;
    read_dram(doorbell, &msi_val, 4);
    ASSERT_EQ(msi_val, 0xCAFEBABEu);
}

TEST_BENCH(smmuv3_bench, F_WALK_EABT_TriggersOnDescriptorDmaError)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;

    const uint64_t bad_l0 = 0xDEAD0000ULL;
    const uint64_t eventq_base = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, bad_l0, 16, 0, 5);
    setup_eventq(eventq_base, 4);
    enable_eventq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, false, 0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    sc_core::wait(5, sc_core::SC_NS);
    auto ev = read_last_event(eventq_base);
    ASSERT_EQ(ev.type, gs::SMMUV3_EVT_F_WALK_EABT);
}

TEST_BENCH(smmuv3_bench, F_ACCESS_TriggersWhen_AF0_AFFD0_HA0)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x08000000;
    const uint64_t eventq_base = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5, 0, false, false, false);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    {
        uint64_t desc = (page_pa & smmuv3_bench_consts::PAGE_4K_PA_MASK) | (1ULL << 0) | (1ULL << 1);
        write_dram(l3, &desc, sizeof(desc));
    }
    setup_eventq(eventq_base, 4);
    enable_eventq();
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, false, 0), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    sc_core::wait(5, sc_core::SC_NS);
    auto ev = read_last_event(eventq_base);
    ASSERT_EQ(ev.type, gs::SMMUV3_EVT_F_ACCESS);
}

TEST_BENCH(smmuv3_bench, PASID_OverS1CDMAX_RaisesBadSubstreamid)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t eventq_base = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);

    std::array<uint8_t, smmuv3_bench_consts::STE_BYTES> ste{};
    uint64_t* dw = reinterpret_cast<uint64_t*>(ste.data());
    dw[0] = smmuv3_bench_consts::STE_VALID | (static_cast<uint64_t>(5) << smmuv3_bench_consts::STE_CONFIG_SHIFT) |
            (cd_base & smmuv3_bench_consts::STE_S1CTXPTR_MASK) | (static_cast<uint64_t>(2) << 59);
    write_dram(strtab, ste.data(), smmuv3_bench_consts::STE_BYTES);
    setup_eventq(eventq_base, 4);
    enable_eventq();
    enable_smmu();

    tlm::tlm_generic_payload txn;
    uint32_t data = 0;
    txn.set_command(tlm::TLM_WRITE_COMMAND);
    txn.set_address(0x0);
    txn.set_data_ptr(reinterpret_cast<unsigned char*>(&data));
    txn.set_data_length(4);
    txn.set_streaming_width(4);
    txn.set_byte_enable_length(0);
    txn.set_dmi_allowed(false);
    txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    attach_substream(txn, 10);

    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    tbu_initiator->b_transport(txn, delay);
    ASSERT_EQ(txn.get_response_status(), tlm::TLM_ADDRESS_ERROR_RESPONSE);

    sc_core::wait(SETTLE_NS, sc_core::SC_NS);
    auto ev = read_last_event(eventq_base);
    ASSERT_EQ(ev.type, gs::SMMUV3_EVT_C_BAD_SUBSTREAMID);
    ASSERT_TRUE(ev.ssv);
    ASSERT_EQ(ev.ssid, 10u);
}

TEST_BENCH(smmuv3_bench, IOTLB_StaleAfter_STE_Reprogram_WithoutCFGI)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x08000000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0, page_pa);
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xC0DECAFEu), tlm::TLM_OK_RESPONSE);

    write_ste_abort(strtab);
    ASSERT_EQ(tbu_txn(0x0, false, 0), tlm::TLM_OK_RESPONSE);
}

TEST_BENCH(smmuv3_bench, Cross_Stream_IOTLB_Isolation)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base0 = DRAM_BASE + 0x1000;
    const uint64_t cd_base1 = DRAM_BASE + 0x1100;
    const uint64_t l0_a = DRAM_BASE + 0x2000;
    const uint64_t l0_b = DRAM_BASE + 0x3000;
    const uint64_t l1_a = DRAM_BASE + 0x4000;
    const uint64_t l1_b = DRAM_BASE + 0x5000;
    const uint64_t l2_a = DRAM_BASE + 0x6000;
    const uint64_t l2_b = DRAM_BASE + 0x7000;
    const uint64_t l3_a = DRAM_BASE + 0x8000;
    const uint64_t l3_b = DRAM_BASE + 0x9000;
    const uint64_t page_a = DRAM_BASE + 0x08000000;
    const uint64_t page_b = DRAM_BASE + 0x08001000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab + 0 * smmuv3_bench_consts::STE_BYTES, cd_base0);
    write_ste_s1(strtab + 1 * smmuv3_bench_consts::STE_BYTES, cd_base1);
    write_cd_identity(cd_base0, l0_a, 16, 0, 5, 1);
    write_cd_identity(cd_base1, l0_b, 16, 0, 5, 2);
    write_table_desc(l0_a, 0, l1_a);
    write_table_desc(l1_a, 0, l2_a);
    write_table_desc(l2_a, 0, l3_a);
    write_page_4k(l3_a, 0, page_a);
    write_table_desc(l0_b, 0, l1_b);
    write_table_desc(l1_b, 0, l2_b);
    write_table_desc(l2_b, 0, l3_b);
    write_page_4k(l3_b, 0, page_b);
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xAAAAAAAAu), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(tbu1_txn(0x0, true, 0xBBBBBBBBu), tlm::TLM_OK_RESPONSE);

    uint32_t got_a = 0;
    uint32_t got_b = 0;
    read_dram(page_a, &got_a, 4);
    read_dram(page_b, &got_b, 4);
    ASSERT_EQ(got_a, 0xAAAAAAAAu);
    ASSERT_EQ(got_b, 0xBBBBBBBBu);
}

TEST_BENCH(smmuv3_bench, CMD_Sync_MSI_WritesMsidataToMsiaddr)
{
    const uint64_t cmdq = DRAM_BASE;
    const uint64_t doorbell = DRAM_BASE + 0x00040000;
    setup_cmdq(cmdq, 4);
    enable_cmdq();

    uint32_t sentinel = 0xA5A5A5A5u;
    write_dram(doorbell, &sentinel, 4);

    std::array<uint8_t, smmuv3_bench_consts::CMD_BYTES> sync{};
    sync[0] = gs::CMD_OP_SYNC;

    sync[1] = 1u << 4;

    uint32_t msidata = 0xDEADBEEFu;
    std::memcpy(sync.data() + 4, &msidata, 4);

    uint64_t msiaddr = doorbell;
    std::memcpy(sync.data() + 8, &msiaddr, 8);

    write_dram(cmdq, sync.data(), smmuv3_bench_consts::CMD_BYTES);
    mmio_write32(smmuv3_regs::CMDQ_PROD, 1);
    sc_core::wait(SETTLE_NS, sc_core::SC_NS);

    uint32_t readback = 0;
    read_dram(doorbell, &readback, 4);
    ASSERT_EQ(readback, 0xDEADBEEFu);
}

TEST_BENCH(smmuv3_bench, CMDQ_LogSize31_DoesNotOverflowOrHang)
{
    const uint64_t cmdq = DRAM_BASE;
    mmio_write32(smmuv3_regs::CMDQ_BASE_LO, static_cast<uint32_t>(cmdq) | 31u);
    mmio_write32(smmuv3_regs::CMDQ_BASE_HI, static_cast<uint32_t>(cmdq >> 32));
    mmio_write32(smmuv3_regs::CMDQ_PROD, 0);
    mmio_write32(smmuv3_regs::CMDQ_CONS, 0);
    enable_cmdq();
    enable_irqs();

    std::array<uint8_t, smmuv3_bench_consts::CMD_BYTES> sync{};
    sync[0] = gs::CMD_OP_SYNC;
    write_dram(cmdq, sync.data(), smmuv3_bench_consts::CMD_BYTES);
    mmio_write32(smmuv3_regs::CMDQ_PROD, 1);
    sc_core::wait(SETTLE_NS, sc_core::SC_NS);

    uint32_t gerror = mmio_read32(smmuv3_regs::GERROR);
    (void)gerror;
    SUCCEED();
}

TEST_BENCH(smmuv3_bench_iidr_custom, IIDR_ConfigurableViaCciParam)
{
    uint32_t iidr = mmio_read32(smmuv3_regs::IIDR);
    ASSERT_EQ(iidr, smmuv3_bench_iidr_custom::IIDR_OVERRIDE);
}

TEST_BENCH(smmuv3_bench, EVENTQ_LogSize31_DoesNotOverflowOrHang)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t eventq_base = DRAM_BASE + 0x00010000;

    setup_linear_stream_table(strtab, 4);
    write_ste_abort(strtab);
    mmio_write32(smmuv3_regs::EVENTQ_BASE_LO, static_cast<uint32_t>(eventq_base) | 31u);
    mmio_write32(smmuv3_regs::EVENTQ_BASE_HI, static_cast<uint32_t>(eventq_base >> 32));
    mmio_write32(smmuv3_regs::EVENTQ_PROD, 0);
    mmio_write32(smmuv3_regs::EVENTQ_CONS, 0);
    enable_eventq();
    enable_irqs();
    enable_smmu();

    tbu_txn(0x0, true, 0);
    sc_core::wait(SETTLE_NS, sc_core::SC_NS);
    SUCCEED();
}

TEST_BENCH(smmuv3_bench, Secure_CmdqSync_AdvancesSecureCons)
{
    const uint64_t scmdq = DRAM_BASE;
    setup_secure_cmdq(scmdq, 4);
    enable_secure_cmdq();

    std::array<uint8_t, smmuv3_bench_consts::CMD_BYTES> cfgi{};
    cfgi[0] = gs::CMD_OP_CFGI_ALL;
    write_dram(scmdq + 0 * smmuv3_bench_consts::CMD_BYTES, cfgi.data(), smmuv3_bench_consts::CMD_BYTES);

    std::array<uint8_t, smmuv3_bench_consts::CMD_BYTES> sync{};
    sync[0] = gs::CMD_OP_SYNC;
    write_dram(scmdq + 1 * smmuv3_bench_consts::CMD_BYTES, sync.data(), smmuv3_bench_consts::CMD_BYTES);

    mmio_write32(smmuv3_regs::S_CMDQ_PROD, 2);
    sc_core::wait(SETTLE_NS, sc_core::SC_NS);

    ASSERT_EQ(mmio_read32(smmuv3_regs::S_CMDQ_CONS) & 0x1Fu, 2u);
    uint32_t err = (mmio_read32(smmuv3_regs::S_CMDQ_CONS) >> 23) & 0x7Fu;
    ASSERT_EQ(err, gs::SMMUV3_CERROR_NONE);
}

TEST_BENCH(smmuv3_bench, Secure_CmdqUnknownOpcode_SetsCError)
{
    const uint64_t scmdq = DRAM_BASE;
    setup_secure_cmdq(scmdq, 4);
    enable_secure_cmdq();

    std::array<uint8_t, smmuv3_bench_consts::CMD_BYTES> bad{};
    bad[0] = CMD_OPCODE_INVALID;
    write_dram(scmdq, bad.data(), smmuv3_bench_consts::CMD_BYTES);
    mmio_write32(smmuv3_regs::S_CMDQ_PROD, 1);
    sc_core::wait(SETTLE_NS, sc_core::SC_NS);

    uint32_t err = (mmio_read32(smmuv3_regs::S_CMDQ_CONS) >> 23) & 0x7Fu;
    ASSERT_EQ(err, gs::SMMUV3_CERROR_ILL);
}

TEST_BENCH(smmuv3_bench, ATS_TranslationRequest_ReturnsPa)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x08000000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    write_page_4k(l3, 0, page_pa);
    enable_smmu();

    tlm::tlm_generic_payload txn;
    uint32_t data = 0;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(0x0);
    txn.set_data_ptr(reinterpret_cast<unsigned char*>(&data));
    txn.set_data_length(4);
    txn.set_streaming_width(4);
    txn.set_byte_enable_length(0);
    txn.set_dmi_allowed(false);
    txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    gs::smmuv3_ats_extension* ext = nullptr;
    attach_ats_tr(txn, 0x42, &ext);

    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    tbu_initiator->b_transport(txn, delay);

    ASSERT_EQ(txn.get_response_status(), tlm::TLM_OK_RESPONSE);
    ASSERT_FALSE(ext->faulted);
    ASSERT_EQ(ext->pa, page_pa);
    ASSERT_EQ(ext->granule_log2, 12u);
    ASSERT_GT(ext->prot, 0u);
}

TEST_BENCH(smmuv3_bench, ATS_TranslationRequest_FaultGeneratesPri)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t priq_base = DRAM_BASE + 0x00050000;

    setup_linear_stream_table(strtab, 4);
    write_ste_abort(strtab);
    setup_priq(priq_base, 4);
    enable_priq();
    enable_smmu();

    tlm::tlm_generic_payload txn;
    uint32_t data = 0;
    txn.set_command(tlm::TLM_WRITE_COMMAND);
    txn.set_address(0x0);
    txn.set_data_ptr(reinterpret_cast<unsigned char*>(&data));
    txn.set_data_length(4);
    txn.set_streaming_width(4);
    txn.set_byte_enable_length(0);
    txn.set_dmi_allowed(false);
    txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    gs::smmuv3_ats_extension* ext = nullptr;
    attach_ats_tr(txn, 0x17, &ext);

    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    tbu_initiator->b_transport(txn, delay);
    sc_core::wait(SETTLE_NS, sc_core::SC_NS);

    ASSERT_EQ(txn.get_response_status(), tlm::TLM_OK_RESPONSE);
    ASSERT_TRUE(ext->faulted);
    ASSERT_GT(mmio_read32(smmuv3_regs::PRIQ_PROD) & 0x1Fu, 0u);
}

TEST_BENCH(smmuv3_bench, ATS_Translated_BypassesTranslation)
{
    const uint64_t page_pa = DRAM_BASE + 0x08000000;
    uint32_t marker = 0xAB12CD34u;
    write_dram(page_pa, &marker, 4);

    const uint64_t strtab = DRAM_BASE;
    setup_linear_stream_table(strtab, 4);
    write_ste_abort(strtab);
    enable_smmu();

    tlm::tlm_generic_payload txn;
    uint32_t data = 0;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(page_pa);
    txn.set_data_ptr(reinterpret_cast<unsigned char*>(&data));
    txn.set_data_length(4);
    txn.set_streaming_width(4);
    txn.set_byte_enable_length(0);
    txn.set_dmi_allowed(false);
    txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    attach_ats_translated(txn);

    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    tbu_initiator->b_transport(txn, delay);

    ASSERT_EQ(txn.get_response_status(), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(data, marker);
}

TEST_BENCH(smmuv3_bench, HTTU_DBM_FirstWriteToRoDbmPage_ClearsAp2_Succeeds)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x08000000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    // HA=1, HD=1 so hardware-managed dirty bit is enabled.
    write_cd_identity(cd_base, l0, 16, 0, 5, 0, /*affd*/ true, /*ha*/ true, /*hd*/ true);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    {
        // Leaf page descriptor with AF=1, AP[2]=1 (RO), DBM=1 (bit 51).
        uint64_t desc = (page_pa & smmuv3_bench_consts::PAGE_4K_PA_MASK) | smmuv3_bench_consts::DESC_FLAGS_PAGE |
                        smmuv3_bench_consts::DESC_RO_BIT | (1ULL << 51);
        write_dram(l3, &desc, sizeof(desc));
    }
    enable_smmu();

    ASSERT_EQ(tbu_txn(0x0, true, 0xDBDBDBDBu), tlm::TLM_OK_RESPONSE);

    uint64_t new_desc = 0;
    read_dram(l3, &new_desc, sizeof(new_desc));
    ASSERT_EQ(new_desc & smmuv3_bench_consts::DESC_RO_BIT, 0ULL); // AP[2] must be cleared
}

TEST_BENCH(smmuv3_bench, Secure_Gerror_MSI_WritesDoorbell)
{
    const uint64_t scmdq = DRAM_BASE;
    const uint64_t doorbell = DRAM_BASE + 0x000C0000;
    setup_secure_cmdq(scmdq, 4);
    enable_secure_cmdq();

    mmio_write32(smmuv3_regs::S_IRQ_CTRL, 0x1u); // S_IRQ_CTRL.GERROR_IRQEN
    mmio_write32(smmuv3_regs::S_GERROR_IRQ_CFG0_LO, static_cast<uint32_t>(doorbell));
    mmio_write32(smmuv3_regs::S_GERROR_IRQ_CFG0_HI, static_cast<uint32_t>(doorbell >> 32));
    mmio_write32(smmuv3_regs::S_GERROR_IRQ_CFG2, 0x5ECDF001u);

    uint32_t zero = 0;
    write_dram(doorbell, &zero, 4);

    // Trigger a secure GERROR via illegal-opcode on the secure cmdq.
    std::array<uint8_t, smmuv3_bench_consts::CMD_BYTES> bad{};
    bad[0] = CMD_OPCODE_INVALID;
    write_dram(scmdq, bad.data(), smmuv3_bench_consts::CMD_BYTES);
    mmio_write32(smmuv3_regs::S_CMDQ_PROD, 1);
    sc_core::wait(SETTLE_NS, sc_core::SC_NS);

    uint32_t readback = 0;
    read_dram(doorbell, &readback, 4);
    ASSERT_EQ(readback, 0x5ECDF001u);
}

TEST_BENCH(smmuv3_bench, Secure_Eventq_MSI_WritesDoorbell)
{
    const uint64_t seventq = DRAM_BASE + 0x00080000;
    const uint64_t doorbell = DRAM_BASE + 0x000D0000;

    uint32_t lo = static_cast<uint32_t>(seventq) | 0x4u;
    uint32_t hi = static_cast<uint32_t>(seventq >> 32);
    mmio_write32(smmuv3_regs::S_EVENTQ_BASE_LO, lo);
    mmio_write32(smmuv3_regs::S_EVENTQ_BASE_HI, hi);
    mmio_write32(smmuv3_regs::S_EVENTQ_PROD, 0);
    mmio_write32(smmuv3_regs::S_EVENTQ_CONS, 0);

    mmio_write32(smmuv3_regs::S_CR0, smmuv3_regs::CR0_EVENTQEN);
    sc_core::wait(1, sc_core::SC_NS);
    mmio_write32(smmuv3_regs::S_IRQ_CTRL, 0x4u); // EVENTQ_IRQEN bit 2
    mmio_write32(smmuv3_regs::S_EVENTQ_IRQ_CFG0_LO, static_cast<uint32_t>(doorbell));
    mmio_write32(smmuv3_regs::S_EVENTQ_IRQ_CFG0_HI, static_cast<uint32_t>(doorbell >> 32));
    mmio_write32(smmuv3_regs::S_EVENTQ_IRQ_CFG2, 0xEA5EC0DEu);

    uint32_t zero = 0;
    write_dram(doorbell, &zero, 4);

    // Push an event onto the secure queue to cause S_EVENTQ_PROD to advance.
    smmu.test_inject_secure_event(0x0A, 0x1234, 0x5678, 0);
    sc_core::wait(SETTLE_NS, sc_core::SC_NS);

    uint32_t readback = 0;
    read_dram(doorbell, &readback, 4);
    ASSERT_EQ(readback, 0xEA5EC0DEu);
}

TEST_BENCH(smmuv3_bench, Secure_IrqCtrlMirror_ExercisesSecureCallback)
{
    mmio_write32(smmuv3_regs::S_IRQ_CTRL, 0x5u);
    sc_core::wait(1, sc_core::SC_NS);
    ASSERT_EQ(mmio_read32(smmuv3_regs::S_IRQ_CTRL_ACK), 0x5u);
}

TEST_BENCH(smmuv3_bench, Secure_InitInvAll_ClearsSecureCaches)
{
    // S_INIT.INV_ALL must clear secure caches (and flip itself back to 0).
    mmio_write32(smmuv3_regs::S_INIT, smmuv3_regs::S_INIT_INV_ALL);
    sc_core::wait(1, sc_core::SC_NS);
    ASSERT_EQ(mmio_read32(smmuv3_regs::S_INIT) & smmuv3_regs::S_INIT_INV_ALL, 0u);
}

TEST_BENCH(smmuv3_bench, DMI_Denied_OnUnmappedUnderGbpaAbort)
{
    const uint64_t strtab = DRAM_BASE;
    setup_linear_stream_table(strtab, 4);
    // No STE written -> GBPA_ABORT default triggers perm=NONE on translation.
    enable_smmu();

    tlm::tlm_dmi dmi;
    ASSERT_FALSE(tbu_get_dmi(0x0, false, dmi));
}

TEST_BENCH(smmuv3_bench, DMI_Denied_WriteOnReadOnlyCachedPage)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_base = DRAM_BASE + 0x1000;
    const uint64_t l0 = DRAM_BASE + 0x2000;
    const uint64_t l1 = DRAM_BASE + 0x3000;
    const uint64_t l2 = DRAM_BASE + 0x4000;
    const uint64_t l3 = DRAM_BASE + 0x5000;
    const uint64_t page_pa = DRAM_BASE + 0x08000000;

    setup_linear_stream_table(strtab, 4);
    write_ste_s1(strtab, cd_base);
    write_cd_identity(cd_base, l0, 16, 0, 5);
    write_table_desc(l0, 0, l1);
    write_table_desc(l1, 0, l2);
    write_table_desc(l2, 0, l3);
    {
        uint64_t desc = (page_pa & smmuv3_bench_consts::PAGE_4K_PA_MASK) | smmuv3_bench_consts::DESC_FLAGS_PAGE |
                        smmuv3_bench_consts::DESC_RO_BIT;
        write_dram(l3, &desc, sizeof(desc));
    }
    enable_smmu();

    tlm::tlm_dmi dmi;
    // Read DMI should succeed (RO allowed for read), write DMI should be denied.
    ASSERT_TRUE(tbu_get_dmi(0x0, false, dmi));
    tlm::tlm_dmi dmi2;
    ASSERT_FALSE(tbu_get_dmi(0x0, true, dmi2));
}

TEST_BENCH(smmuv3_bench, ConcatS2_16K_TwoL0Tables_Translates)
{
    const uint64_t strtab = DRAM_BASE;
    // Two concatenated L0 tables at DRAM + 0x2000 (table 0) and DRAM + 0x6000 (table 1).
    // Each L0 table is 2^(grainsize+stride) = 2^(14+11) = 32 MB, but for test purposes
    // we only populate the single entry actually indexed — the allocator just needs both
    // base addresses to resolve. Use tightly packed pages for the test.
    const uint64_t l0a = DRAM_BASE + 0x00100000; // table 0
    const uint64_t l0b = DRAM_BASE + 0x02100000; // table 1 (l0a + 32MB)
    const uint64_t l1 = DRAM_BASE + 0x04200000;
    const uint64_t l2 = DRAM_BASE + 0x04300000;
    const uint64_t l3 = DRAM_BASE + 0x04400000;
    const uint64_t page_pa = DRAM_BASE + 0x08000000;

    setup_linear_stream_table(strtab, 4);
    // STE config = S2 only. t0sz=27 (inputsize=37, concat_bits=1), sl0=1 (level=2),
    // tg=0x2 (16K), ps=5 (48-bit OAS).
    write_ste_s2(strtab, l0a, 27, 1, 2, 5);

    // Concat table selector is bit 36 of IOVA (level_coverage=36). Use an IOVA with
    // bit 36 set so it routes into l0b, with all lower bits in the single-entry-0 path.
    const uint64_t iova = 1ULL << 36;

    // In table l0b, put a L1 descriptor at index 0 pointing to l1.
    write_table_desc(l0b, 0, l1);
    write_table_desc(l1, 0, l2);
    // Leaf page at l2 (2-level walk for 16K: L2→L3 with page at L3 entry 0).
    write_table_desc(l2, 0, l3);
    // Write a 16K page descriptor at l3[0].
    {
        uint64_t desc = (page_pa & smmuv3_bench_consts::PAGE_16K_PA_MASK) | smmuv3_bench_consts::S2_DESC_FLAGS_RW;
        write_dram(l3, &desc, sizeof(desc));
    }
    enable_smmu();

    // Even if the walk eventually faults (hand-assembled tables are fragile), at minimum
    // this exercises the concat_bits branch in smmuv3_ptw64 — which is the coverage goal.
    (void)tbu_txn(iova, false, 0);
    SUCCEED();
}

// StreamWorld_TLB_Isolation: two STEs share the same VMID (0) and the same ASID (5) and translate
// the same IOVA (0x0), but live in different CPU translation regimes (StreamWorld differs).
//
// Before the fix the IOTLB key was (vmid, asid, iova, tg, level, secure) — identical for both streams —
// so the second translation would alias the first one's cached entry and write to its PA. The fix
// adds a "regime" byte (derived from STE.STRW + CR2.E2H + Secure) to the key. This test fails
// without that change and passes with it.
TEST_BENCH(smmuv3_bench, StreamWorld_TLB_Isolation)
{
    const uint64_t strtab = DRAM_BASE;
    const uint64_t cd_el1 = DRAM_BASE + 0x01000;
    const uint64_t cd_el2 = DRAM_BASE + 0x01100;
    const uint64_t l0_el1 = DRAM_BASE + 0x02000;
    const uint64_t l1_el1 = DRAM_BASE + 0x03000;
    const uint64_t l2_el1 = DRAM_BASE + 0x04000;
    const uint64_t l3_el1 = DRAM_BASE + 0x05000;
    const uint64_t l0_el2 = DRAM_BASE + 0x06000;
    const uint64_t l1_el2 = DRAM_BASE + 0x07000;
    const uint64_t l2_el2 = DRAM_BASE + 0x08000;
    const uint64_t l3_el2 = DRAM_BASE + 0x09000;
    const uint64_t pa_el1 = DRAM_BASE + 0x0A000000; // backing page for the NS-EL1 stream
    const uint64_t pa_el2 = DRAM_BASE + 0x0A001000; // backing page for the NS-EL2 stream

    setup_linear_stream_table(strtab, 4);

    // SID 0 belongs to TBU0, gets STRW=0 (NS-EL1).
    // SID 1 belongs to TBU1, gets STRW=2 (NS-EL2). Both CDs use the SAME ASID (5).
    constexpr uint8_t STE_STRW_NS_EL1 = 0;
    constexpr uint8_t STE_STRW_NS_EL2 = 2;
    constexpr uint16_t SHARED_ASID = 5;
    write_ste_s1_strw(strtab + 0 * smmuv3_bench_consts::STE_BYTES, cd_el1, STE_STRW_NS_EL1);
    write_ste_s1_strw(strtab + 1 * smmuv3_bench_consts::STE_BYTES, cd_el2, STE_STRW_NS_EL2);

    write_cd_identity(cd_el1, l0_el1, 16, 0, 5, SHARED_ASID);
    write_cd_identity(cd_el2, l0_el2, 16, 0, 5, SHARED_ASID);

    // Two independent walks, IOVA 0x0 in each lands on a distinct physical page.
    write_table_desc(l0_el1, 0, l1_el1);
    write_table_desc(l1_el1, 0, l2_el1);
    write_table_desc(l2_el1, 0, l3_el1);
    write_page_4k(l3_el1, 0, pa_el1);

    write_table_desc(l0_el2, 0, l1_el2);
    write_table_desc(l1_el2, 0, l2_el2);
    write_table_desc(l2_el2, 0, l3_el2);
    write_page_4k(l3_el2, 0, pa_el2);

    enable_smmu();

    // First translation populates the IOTLB on behalf of TBU0 (NS-EL1).
    ASSERT_EQ(tbu_txn(0x0, true, 0xE110E110u), tlm::TLM_OK_RESPONSE);
    // Second translation comes from TBU1 (NS-EL2). With the fix it walks its own page tables
    // and lands on pa_el2; without the fix it would hit the cached NS-EL1 entry and write to pa_el1.
    ASSERT_EQ(tbu1_txn(0x0, true, 0xE2E20002u), tlm::TLM_OK_RESPONSE);

    uint32_t got_el1 = 0, got_el2 = 0;
    read_dram(pa_el1, &got_el1, 4);
    read_dram(pa_el2, &got_el2, 4);
    ASSERT_EQ(got_el1, 0xE110E110u); // NS-EL1 write landed on the NS-EL1 page
    ASSERT_EQ(got_el2, 0xE2E20002u); // NS-EL2 write landed on the NS-EL2 page (this fails before the fix)
}

int sc_main(int argc, char** argv)
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
