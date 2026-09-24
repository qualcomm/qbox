/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "smmu500-bench.h"

TEST_BENCH(smmu500_test_bench, S2CRBypass)
{
    configure_stream(smmu500_test_regs::S2CR_BYPASS);

    const auto entry = translate();
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_EQ(entry.addr_mask, static_cast<uint64_t>(-1));
    ASSERT_EQ(entry.translated_addr, 0x1234u);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), 0u);
}

TEST_BENCH(smmu500_test_bench, ResetAndIdentificationRegisters)
{
    ASSERT_EQ(mmio_read32(smmu500_test_regs::SIDR0) & 0xffu, 2u);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::SIDR1) & 0xffu, 1u);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::SIDR2), 0x7111u);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::SIDR7), 0x21u);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::SCR0), 0x00200001u);
}

TEST_BENCH(smmu500_test_bench, IdentificationRegistersIgnoreWrites)
{
    mmio_write32(smmu500_test_regs::SIDR0, 0xffffffffu);
    mmio_write32(smmu500_test_regs::SIDR1, 0xffffffffu);
    mmio_write32(smmu500_test_regs::SIDR2, 0xffffffffu);
    mmio_write32(smmu500_test_regs::SIDR7, 0xffffffffu);

    ASSERT_EQ(mmio_read32(smmu500_test_regs::SIDR0) & 0xffu, 2u);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::SIDR1) & 0xffu, 1u);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::SIDR2), 0x7111u);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::SIDR7), 0x21u);
}

TEST_BENCH(smmu500_test_bench, ClientPortDisableBypassesBeforeStreamLookup)
{
    smmu.SMMU_NSCR0 = 1u;

    const auto entry = translate();
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_EQ(entry.addr_mask, static_cast<uint64_t>(-1));
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), 0u);
}

TEST_BENCH(smmu500_test_bench, StreamMatchHonorsMask)
{
    configure_stream(smmu500_test_regs::S2CR_BYPASS);
    smmu.SMMU_SMR[0][smmu.SMR_ID] = 0x12;
    smmu.SMMU_SMR[0][smmu.SMR_MASK] = 0x3;

    ASSERT_EQ(smmu.smmu500_stream_id_match(0x12), 0);
    ASSERT_EQ(smmu.smmu500_stream_id_match(0x13), 0);
    ASSERT_EQ(smmu.smmu500_stream_id_match(0x10), 0);
}

TEST_BENCH(smmu500_test_bench, InvalidContextFaultRejectsWithGFRE)
{
    configure_stream(smmu500_test_regs::S2CR_FAULT);
    smmu.SMMU_NSCR0 = smmu500_test_regs::GFRE;

    const auto entry = translate();
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_NONE);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), smmu500_test_regs::ICF);
}

TEST_BENCH(smmu500_test_bench, InvalidContextFaultBypassesWithoutGFRE)
{
    configure_stream(smmu500_test_regs::S2CR_FAULT);

    const auto entry = translate();
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_EQ(entry.addr_mask, static_cast<uint64_t>(-1));
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), smmu500_test_regs::ICF);
}

TEST_BENCH(smmu500_test_bench, ReservedS2CRTypeReportsICF)
{
    configure_stream(3);
    smmu.SMMU_NSCR0 = smmu500_test_regs::GFRE;

    const auto entry = translate();
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_NONE);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), smmu500_test_regs::ICF);
}

TEST_BENCH(smmu500_test_bench, InvalidContextBankReportsICF)
{
    configure_stream(smmu500_test_regs::S2CR_TRANSLATION, 1);
    smmu.SMMU_NSCR0 = smmu500_test_regs::GFRE;

    const auto entry = translate();
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_NONE);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), smmu500_test_regs::ICF);
}

TEST_BENCH(smmu500_test_bench, OutOfRangeS2CRContextBankReportsICF)
{
    configure_stream(smmu500_test_regs::S2CR_TRANSLATION, 0xff);
    smmu.SMMU_NSCR0 = smmu500_test_regs::GFRE;

    const auto entry = translate();
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_NONE);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), smmu500_test_regs::ICF);
}

TEST_BENCH(smmu500_test_bench, UnidentifiedStreamFault)
{
    smmu.SMMU_NSCR0 = smmu500_test_regs::USFCFG | smmu500_test_regs::GFRE;

    const auto entry = translate();
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_NONE);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), smmu500_test_regs::USF);
}

TEST_BENCH(smmu500_test_bench, TransportTranslationDecodesTransportStreamId)
{
    configure_identity_context();
    smmu.SMMU_SMR[0][smmu.SMR_ID] = 3;
    smmu.SMMU_NSCR0 = smmu500_test_regs::USFCFG | smmu500_test_regs::GFRE;

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(0x0000000300001234ULL);
    ASSERT_EQ(smmu.smmu500_translate(txn, 0, true).perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), 0u);
}

TEST_BENCH(smmu500_test_bench, StreamIdCompositionUsesBaseSid)
{
    constexpr uint32_t tbu_id = 8;
    constexpr uint32_t topo_id = 14;
    constexpr uint32_t csid = 1;
    constexpr uint32_t base_sid = (tbu_id << 10) | (topo_id << 5);
    constexpr uint32_t expected_sid = base_sid | csid;

    ASSERT_EQ(expected_sid, 0x21c1u);
    ASSERT_EQ(gs::smmu500<>::smmu500_compose_stream_id(expected_sid, 0), 0x21c1u);
    ASSERT_EQ(gs::smmu500<>::smmu500_compose_stream_id(base_sid, 0x0000000100000000ULL), 0x21c1u);
}

TEST_BENCH(smmu500_test_bench, TransportBitsSelectConfiguredCsid)
{
    configure_identity_context();
    smmu.SMMU_SMR[0][smmu.SMR_ID] = 0x21c1;
    smmu.SMMU_SMR[0][smmu.SMR_MASK] = 0;
    smmu.SMMU_SMR[0][smmu.SMR_VALID] = 1;

    int cb = -1;
    ASSERT_EQ(smmu.smmu500_resolve_stream_id(0x21c0, 0x0000000100000000ULL, &cb), 0x21c1u);
    ASSERT_EQ(cb, 0);

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(0x0000000100001234ULL);
    ASSERT_EQ(smmu.smmu500_translate(txn, 0x21c0, true).perm, gs::smmu500<>::IOMMU_RW);
}

TEST_BENCH(smmu500_test_bench, BaseSidDoesNotSelectProgrammedCsid)
{
    configure_identity_context();
    smmu.SMMU_SMR[0][smmu.SMR_ID] = 0x21c1;
    smmu.SMMU_SMR[0][smmu.SMR_MASK] = 0;
    smmu.SMMU_SMR[0][smmu.SMR_VALID] = 1;

    int cb = -1;
    ASSERT_EQ(smmu.smmu500_resolve_stream_id(0x21c0, 0, &cb), 0x21c0u);
    ASSERT_EQ(cb, -1);

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(0x1234);
    smmu.SMMU_NSCR0 = smmu500_test_regs::USFCFG;
    const auto entry = smmu.smmu500_translate(txn, 0x21c0);
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_EQ(entry.addr_mask, static_cast<uint64_t>(-1));
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), smmu500_test_regs::USF);
}

TEST_BENCH(smmu500_test_bench, TransportCsidOverridesConfiguredCsid)
{
    constexpr uint32_t base_sid = (17u << 10) | (9u << 5) | 3u;
    constexpr uint32_t expected_sid = (17u << 10) | (9u << 5) | 2u;

    ASSERT_EQ(gs::smmu500<>::smmu500_compose_stream_id(base_sid, 0x0000000200000000ULL), expected_sid);
}

TEST_BENCH(smmu500_test_bench, TransportCsidUsesOnlyAddressBits35To32)
{
    constexpr uint32_t base_sid = (3u << 10) | (5u << 5);
    constexpr uint32_t expected_sid = base_sid | 0xau;

    ASSERT_EQ(gs::smmu500<>::smmu500_compose_stream_id(base_sid, 0xabc0000a00000000ULL), expected_sid);
}

TEST_BENCH(smmu500_test_bench, TaggedAddressWithoutCsidUsesConfiguredCompatibilityPath)
{
    smmu.SMMU_NSCR0 = smmu500_test_regs::USFCFG | smmu500_test_regs::GFRE;

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(0x0000000012345678ULL);

    const auto entry = smmu.smmu500_translate(txn, 0, true);
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_EQ(entry.addr_mask, static_cast<uint64_t>(-1));
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), 0u);
}

TEST_BENCH(smmu500_test_bench, TBUWithoutAddressCarriedCsidPreservesCompatibilityBypass)
{
    smmu.SMMU_NSCR0 = smmu500_test_regs::USFCFG | smmu500_test_regs::GFRE;

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(0x10000001234ULL);

    const auto entry = smmu.smmu500_translate(txn, 0x21c0, true);
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_EQ(entry.addr_mask, static_cast<uint64_t>(-1));
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), 0u);
}

TEST_BENCH(smmu500_test_bench, UnidentifiedStreamBypassesWhenUSFDisabled)
{
    smmu.SMMU_NSCR0 = 0;

    const auto entry = translate();
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_EQ(entry.addr_mask, static_cast<uint64_t>(-1));
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), 0u);
}

TEST_BENCH(smmu500_test_bench, UnidentifiedStreamRecordsUSFWhileBypassing)
{
    smmu.SMMU_NSCR0 = smmu500_test_regs::USFCFG;

    const auto entry = translate();
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_EQ(entry.addr_mask, static_cast<uint64_t>(-1));
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), smmu500_test_regs::USF);
}

TEST_BENCH(smmu500_test_bench, StreamMatchConflictFault)
{
    configure_stream(smmu500_test_regs::S2CR_TRANSLATION);
    smmu.SMMU_SMR[1][smmu.SMR_ID] = 0;
    smmu.SMMU_SMR[1][smmu.SMR_MASK] = 0;
    smmu.SMMU_SMR[1][smmu.SMR_VALID] = 1;
    smmu.SMMU_S2CR[1][smmu.S2CR_TYPE] = smmu500_test_regs::S2CR_TRANSLATION;
    smmu.SMMU_NSCR0 = smmu500_test_regs::SMCFCFG | smmu500_test_regs::GFRE;

    const auto entry = translate();
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_NONE);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), smmu500_test_regs::SMCF);
}

TEST_BENCH(smmu500_test_bench, StreamMatchConflictBypassesWhenSMCFDisabled)
{
    configure_stream(smmu500_test_regs::S2CR_BYPASS);
    smmu.SMMU_SMR[1][smmu.SMR_ID] = 0;
    smmu.SMMU_SMR[1][smmu.SMR_MASK] = 0;
    smmu.SMMU_SMR[1][smmu.SMR_VALID] = 1;
    smmu.SMMU_S2CR[1][smmu.S2CR_TYPE] = smmu500_test_regs::S2CR_BYPASS;

    const auto entry = translate();
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_EQ(entry.addr_mask, static_cast<uint64_t>(-1));
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), 0u);
}

TEST_BENCH(smmu500_test_bench, StreamMatchConflictUsesFirstMatchWhenSMCFDisabled)
{
    configure_page_table(0x8000);
    smmu.SMMU_SMR[1][smmu.SMR_ID] = 0;
    smmu.SMMU_SMR[1][smmu.SMR_MASK] = 0;
    smmu.SMMU_SMR[1][smmu.SMR_VALID] = 1;
    smmu.SMMU_S2CR[1][smmu.S2CR_TYPE] = smmu500_test_regs::S2CR_BYPASS;

    const auto entry = translate();
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_EQ(entry.translated_addr, 0x8000u);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), 0u);
}

TEST_BENCH(smmu500_test_bench, StreamMatchConflictRecordsSMCFWhileBypassing)
{
    configure_stream(smmu500_test_regs::S2CR_BYPASS);
    smmu.SMMU_SMR[1][smmu.SMR_ID] = 0;
    smmu.SMMU_SMR[1][smmu.SMR_MASK] = 0;
    smmu.SMMU_SMR[1][smmu.SMR_VALID] = 1;
    smmu.SMMU_S2CR[1][smmu.S2CR_TYPE] = smmu500_test_regs::S2CR_BYPASS;
    smmu.SMMU_NSCR0 = smmu500_test_regs::SMCFCFG;

    const auto entry = translate();
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_EQ(entry.addr_mask, static_cast<uint64_t>(-1));
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), smmu500_test_regs::SMCF);
}

TEST_BENCH(smmu500_test_bench, NSGFSRWriteOneToClear)
{
    configure_stream(smmu500_test_regs::S2CR_FAULT);
    translate();
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), smmu500_test_regs::ICF);

    mmio_write32(smmu500_test_regs::NSGFSR, smmu500_test_regs::ICF);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::NSGFSR), 0u);
}

TEST_BENCH(smmu500_test_bench, NSGFSRWriteOneToClearPreservesOtherFaults)
{
    configure_stream(smmu500_test_regs::S2CR_FAULT);
    translate();
    smmu.SMMU_SMR[0][smmu.SMR_VALID] = 0;
    smmu.SMMU_NSCR0 = smmu500_test_regs::USFCFG;
    translate();
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR),
              smmu500_test_regs::ICF | smmu500_test_regs::USF);

    mmio_write32(smmu500_test_regs::NSGFSR, smmu500_test_regs::ICF);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::NSGFSR), smmu500_test_regs::USF);
}

TEST_BENCH(smmu500_test_bench, NSCR0IsIndependentFromSCR0)
{
    const uint32_t value = smmu500_test_regs::USFCFG | smmu500_test_regs::GFRE;
    mmio_write32(smmu500_test_regs::NSCR0, value);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::NSCR0), value);
    ASSERT_NE(mmio_read32(smmu500_test_regs::SCR0), value);
}

TEST_BENCH(smmu500_test_bench, RegisterAccessSeparatesSecurityStates)
{
    constexpr uint64_t cb0_sctlr = 16 * SMMU_PAGESIZE;
    smmu.SMMU_SCR0 = 0;

    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::SCR0, smmu500_test_regs::GFRE, false),
              tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_SCR0), 0u);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSCR0), smmu500_test_regs::GFRE);
    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::NSCR0, smmu500_test_regs::GFRE, false),
              tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::NSCR0, smmu500_test_regs::GFRE, true),
              tlm::TLM_OK_RESPONSE);

    smmu.SCR1_NSNUMCBO = 0;
    ASSERT_EQ(mmio_write32_with_security(cb0_sctlr, 1, false), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_EQ(mmio_write32_with_security(cb0_sctlr, 1, true), tlm::TLM_OK_RESPONSE);
}

TEST_BENCH(smmu500_test_bench, NonSecureGlobalStatusFrameIsAccessible)
{
    uint32_t value = UINT32_MAX;

    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::NSTLBGSTATUS, false, &value),
              tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(value, 0u);
}

TEST_BENCH(smmu500_test_bench, SecureControlWriteUpdatesTranslationAndInvalidatesBypassDMI)
{
    smmu.SMMU_SCR0 = 1;
    tlm::tlm_dmi dmi;
    ASSERT_TRUE(tbu_get_dmi_with_security(0x1234, dmi, true));
    dmi_invalidations.clear();

    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::SCR0, smmu500_test_regs::GFRE, true),
              tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_SCR0), smmu500_test_regs::GFRE);
    ASSERT_EQ(dmi_invalidations.size(), 1u);
    ASSERT_EQ(dmi_invalidations[0].first, dmi.get_start_address());
    ASSERT_EQ(dmi_invalidations[0].second, dmi.get_end_address());
}

TEST_BENCH(smmu500_test_bench, NonSecureRegisterAccessPermitsSACRReadAndWrite)
{
    constexpr uint32_t sacr_value = 0x5a5a5a5a;

    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::SACR, false), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::SACR, sacr_value, false),
              tlm::TLM_OK_RESPONSE);
}

TEST_BENCH(smmu500_test_bench, SACRIsBankedBySecurityState)
{
    constexpr uint32_t secure_value = 0x11111111;
    constexpr uint32_t non_secure_value = 0x22222222;
    constexpr uint32_t alias_value = 0x33333333;
    uint32_t value = 0;

    smmu.SMMU_SACR = secure_value;
    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::SACR, non_secure_value, false),
              tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_SACR), secure_value);
    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::SACR, false, &value), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(value, non_secure_value);

    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::NSACR, alias_value, true),
              tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::SACR, false, &value), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(value, alias_value);
    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::NSACR, 0, false), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::SACR, false, &value), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(value, 0u);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_SACR), secure_value);
}

TEST_BENCH(smmu500_test_bench, SecureAliasesSelectTheNonSecureBank)
{
    uint32_t value = 0;

    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::SCR2, 0x11111111, true), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::SCR2, 0x22222222, false), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::NSCR2, 0x33333333, true), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::SCR2, true, &value), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(value, 0x11111111u);
    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::SCR2, false, &value), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(value, 0x33333333u);
    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::NSCR2, false, &value), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(value, 0x33333333u);
}

TEST_BENCH(smmu500_test_bench, GASRAERestrictsNonSecureGlobalAccess)
{
    uint32_t value = 0;
    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::TBU_PWR_STATUS, false, &value), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::SCR1, smmu500_test_regs::GASRAE, true),
              tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::SCR0, 0, false), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_EQ(mmio_write32_with_security(0x800, 0, false), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::TBU_PWR_STATUS, false, &value),
              tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::NSTLBGSTATUS, false, &value),
              tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::NSTLBGSTATUS, true, &value),
              tlm::TLM_OK_RESPONSE);
}

TEST_BENCH(smmu500_test_bench, NonSecureIdentificationRegistersUseSecureAllocation)
{
    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::SCR1, 0x00000100, true), tlm::TLM_OK_RESPONSE);

    uint32_t value = 0;
    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::SIDR0, false, &value), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(value & 0xffu, 1u);
    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::SIDR1, false, &value), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(value & 0xffu, 0u);
    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::SIDR0, 0xffffffffu, false), tlm::TLM_OK_RESPONSE);
}

TEST_BENCH(smmu500_test_bench, NonSecureGATCommandsAndAliasesUseTheNonSecureBank)
{
    configure_identity_context();
    mmio_write32(smmu500_test_regs::GPAR, 0xdeadbeefu);

    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::GATS1UR, 0x1000, false), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::GATS1UR_H, 0, false), tlm::TLM_OK_RESPONSE);

    uint32_t value = 0;
    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::GPAR, false, &value), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(value, 0x1000u);
    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::GPAR, true, &value), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(value, 0xdeadbeefu);

    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::NSGATS1UR, 0x2000, true), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::NSGATS1UR_H, 0, true), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::NSGPAR, true, &value), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(value, 0x2000u);

    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::NSGATS1UR, 0x3000, false), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::NSGATS1UR_H, 0, false), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::NSGPAR, false, &value), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(value, 0x3000u);
    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::NSGPAR, true, &value), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(value, 0x3000u);
    ASSERT_EQ(mmio_read32_with_security(smmu500_test_regs::GPAR, true, &value), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(value, 0xdeadbeefu);
}

TEST_BENCH(smmu500_test_bench, SecureGlobalFaultUsesSecureControlAndStatusBank)
{
    configure_stream(smmu500_test_regs::S2CR_TRANSLATION);
    smmu.SMMU_SMR[0][smmu.SMR_VALID] = 0;
    smmu.SMMU_SCR0 = smmu500_test_regs::USFCFG | smmu500_test_regs::GFRE;

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(0x1234);
    gs::QemuMemTxSecureTlmExtension security;
    security.value = true;
    txn.set_extension(&security);

    ASSERT_EQ(smmu.smmu500_translate(txn, 0).perm, gs::smmu500<>::IOMMU_NONE);
    txn.clear_extension(&security);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_SGFSR), smmu500_test_regs::USF);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), 0u);
}

TEST_BENCH(smmu500_test_bench, SharedSecurityExtensionOverridesLegacySMMU500Attributes)
{
    configure_stream(smmu500_test_regs::S2CR_TRANSLATION);
    smmu.SMMU_SMR[0][smmu.SMR_VALID] = 0;
    smmu.SMMU_SCR0 = smmu500_test_regs::USFCFG | smmu500_test_regs::GFRE;

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(0x1234);
    gs::smmu500_transaction_attrs_extension attrs;
    attrs.non_secure = true;
    gs::QemuMemTxSecureTlmExtension security;
    security.value = true;
    txn.set_extension(&attrs);
    txn.set_extension(&security);

    ASSERT_EQ(smmu.smmu500_translate(txn, 0).perm, gs::smmu500<>::IOMMU_NONE);
    txn.clear_extension(&security);
    txn.clear_extension(&attrs);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_SGFSR), smmu500_test_regs::USF);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), 0u);
}

TEST_BENCH(smmu500_test_bench, SecurePageTableWalkPropagatesSecurityState)
{
    configure_page_table(0x8000);
    memory.clear_security_states();

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(0x1234);
    gs::QemuMemTxSecureTlmExtension security;
    security.value = true;
    txn.set_extension(&security);

    const auto entry = smmu.smmu500_translate(txn, 0);
    txn.clear_extension(&security);

    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_FALSE(memory.security_states.empty());
    for (const auto& state : memory.security_states) {
        ASSERT_TRUE(state.has_value());
        ASSERT_TRUE(*state);
    }
}

TEST_BENCH(smmu500_test_bench, QemuUserAttributeDoesNotAffectTranslation)
{
    configure_page_table(0x8000);
    // AP[1] clear would reject an unprivileged SMMU-specific request.
    write_memory64(0x6008, 0x8000 | 0x3 | (1ULL << 10));

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(0x1234);
    gs::QemuMemTxUserTlmExtension qemu_attrs;
    qemu_attrs.value = true;
    txn.set_extension(&qemu_attrs);

    const auto entry = smmu.smmu500_translate(txn, 0);
    txn.clear_extension(&qemu_attrs);

    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
}

TEST_BENCH(smmu500_test_bench, LegacyTransactionUsesNonSecureGlobalFaultBank)
{
    configure_stream(smmu500_test_regs::S2CR_TRANSLATION);
    smmu.SMMU_SMR[0][smmu.SMR_VALID] = 0;
    smmu.SMMU_NSCR0 = smmu500_test_regs::USFCFG | smmu500_test_regs::GFRE;

    ASSERT_EQ(translate().perm, gs::smmu500<>::IOMMU_NONE);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_SGFSR), 0u);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), smmu500_test_regs::USF);
}

TEST_BENCH(smmu500_test_bench, SecureAndNonSecureGlobalFaultStatusClearIndependently)
{
    configure_stream(smmu500_test_regs::S2CR_FAULT);
    smmu.SMMU_SCR0 = smmu500_test_regs::GFRE;
    smmu.SMMU_NSCR0 = smmu500_test_regs::GFRE;

    tlm::tlm_generic_payload secure_txn;
    secure_txn.set_command(tlm::TLM_READ_COMMAND);
    secure_txn.set_address(0x1234);
    gs::QemuMemTxSecureTlmExtension security;
    security.value = true;
    secure_txn.set_extension(&security);
    ASSERT_EQ(smmu.smmu500_translate(secure_txn, 0).perm, gs::smmu500<>::IOMMU_NONE);
    secure_txn.clear_extension(&security);

    ASSERT_EQ(translate().perm, gs::smmu500<>::IOMMU_NONE);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::SGFSR), smmu500_test_regs::ICF);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::NSGFSR), smmu500_test_regs::ICF);

    mmio_write32(smmu500_test_regs::SGFSR, smmu500_test_regs::ICF);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::SGFSR), 0u);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::NSGFSR), smmu500_test_regs::ICF);

    mmio_write32(smmu500_test_regs::NSGFSR, smmu500_test_regs::ICF);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::NSGFSR), 0u);
}

TEST_BENCH(smmu500_test_bench, ContextFaultSyndromeRecordsRequestSecurityState)
{
    configure_page_table(0x8000, true);

    tlm::tlm_generic_payload secure_txn;
    gs::QemuMemTxSecureTlmExtension security;
    secure_txn.set_command(tlm::TLM_WRITE_COMMAND);
    secure_txn.set_address(0x1234);
    security.value = true;
    secure_txn.set_extension(&security);
    ASSERT_EQ(smmu.smmu500_translate(secure_txn, 0).perm, gs::smmu500<>::IOMMU_NONE);
    secure_txn.clear_extension(&security);
    ASSERT_FALSE(static_cast<uint32_t>(smmu.SMMU_CB_FSYNR0[0]) & smmu500_test_regs::FSYNR0_NSATTR);

    smmu.SMMU_CB_FSR[0] = 0;
    tlm::tlm_generic_payload non_secure_txn;
    non_secure_txn.set_command(tlm::TLM_WRITE_COMMAND);
    non_secure_txn.set_address(0x1234);
    ASSERT_EQ(smmu.smmu500_translate(non_secure_txn, 0).perm, gs::smmu500<>::IOMMU_NONE);
    ASSERT_TRUE(static_cast<uint32_t>(smmu.SMMU_CB_FSYNR0[0]) & smmu500_test_regs::FSYNR0_NSATTR);
}

TEST_BENCH(smmu500_test_bench, GATSIdentityTranslation)
{
    configure_identity_context();
    mmio_write32(smmu500_test_regs::GATS1PR, 0x1000);
    mmio_write32(smmu500_test_regs::GATS1PR_H, 0);

    ASSERT_EQ(mmio_read32(smmu500_test_regs::GPAR), 0x1000u);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::GPAR_H), 0u);
}

TEST_BENCH(smmu500_test_bench, GATSStage1PageWalkTranslation)
{
    configure_page_table(0x8000);
    mmio_write32(smmu500_test_regs::GATS1PR, 0x1000);
    mmio_write32(smmu500_test_regs::GATS1PR_H, 0);

    ASSERT_EQ(mmio_read32(smmu500_test_regs::GPAR), 0x8000u);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::GPAR_H), 0u);
}

TEST_BENCH(smmu500_test_bench, GATSStage1PermissionFaultSetsError)
{
    configure_page_table(0x8000, true);
    mmio_write32(smmu500_test_regs::GATS1PW, 0x1000);
    mmio_write32(smmu500_test_regs::GATS1PW_H, 0);

    ASSERT_EQ(mmio_read32(smmu500_test_regs::GPAR), 0x1001u);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::GPAR_H), 0u);
    ASSERT_TRUE(smmu.SMMU_CB_FSR[0][smmu.CB_FSR_PF]);
    ASSERT_TRUE(mmio_read32(smmu.SMMU_CB_FSYNR0.get_offset()) & smmu500_test_regs::FSYNR0_ATOF);
    ASSERT_FALSE(mmio_read32(smmu.SMMU_CB_FSYNR0.get_offset()) & smmu500_test_regs::FSYNR0_NSATTR);
}

TEST_BENCH(smmu500_test_bench, GATSInvalidContextBankSetsError)
{
    configure_identity_context();
    mmio_write32(smmu500_test_regs::GATS1PR, 0x1001);
    mmio_write32(smmu500_test_regs::GATS1PR_H, 0);

    ASSERT_EQ(mmio_read32(smmu500_test_regs::GPAR), 1u);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::GPAR_H), 0u);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_SGFSR), smmu500_test_regs::ICF);
}

TEST_BENCH(smmu500_test_bench, PermissionFaultRecordsDetailedSyndrome)
{
    configure_page_table(0x8000, true);

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    const uint32_t fsynr0 = mmio_read32(smmu.SMMU_CB_FSYNR0.get_offset());
    ASSERT_EQ(fsynr0 & 0x3u, 3u);
    ASSERT_TRUE(fsynr0 & smmu500_test_regs::FSYNR0_WNR);
    ASSERT_TRUE(fsynr0 & smmu500_test_regs::FSYNR0_PNU);
    ASSERT_FALSE(fsynr0 & smmu500_test_regs::FSYNR0_IND);
    ASSERT_TRUE(fsynr0 & smmu500_test_regs::FSYNR0_NSATTR);
    ASSERT_FALSE(fsynr0 & smmu500_test_regs::FSYNR0_ATOF);
    ASSERT_FALSE(fsynr0 & smmu500_test_regs::FSYNR0_AFR);
    ASSERT_EQ((fsynr0 >> 16) & 0x7Fu, 0u);
    ASSERT_EQ(mmio_read32(smmu.SMMU_CB_FSYNR1.get_offset()), 0u);
}

TEST_BENCH(smmu500_test_bench, GATSUnprivilegedStage1Translation)
{
    configure_page_table(0x8000);
    mmio_write32(smmu500_test_regs::GATS1UR, 0x1000);
    mmio_write32(smmu500_test_regs::GATS1UR_H, 0);

    ASSERT_EQ(mmio_read32(smmu500_test_regs::GPAR), 0x8000u);
}

TEST_BENCH(smmu500_test_bench, GATSUnprivilegedStage1WriteReportsPermissionFault)
{
    configure_page_table(0x8000, true);
    mmio_write32(smmu500_test_regs::GATS1UW, 0x1000);
    mmio_write32(smmu500_test_regs::GATS1UW_H, 0);

    ASSERT_EQ(mmio_read32(smmu500_test_regs::GPAR), 0x1001u);
    ASSERT_TRUE(smmu.SMMU_CB_FSR[0][smmu.CB_FSR_PF]);
    const uint32_t fsynr0 = mmio_read32(smmu.SMMU_CB_FSYNR0.get_offset());
    ASSERT_FALSE(fsynr0 & smmu500_test_regs::FSYNR0_PNU);
    ASSERT_TRUE(fsynr0 & smmu500_test_regs::FSYNR0_ATOF);
}

TEST_BENCH(smmu500_test_bench, GATSUnprivilegedRejectsPrivilegedOnlyMapping)
{
    configure_page_table(0x8000);
    write_memory64(0x6008, 0x8000 | 0x3 | (1ULL << 10));

    mmio_write32(smmu500_test_regs::GATS1UR, 0x1000);
    mmio_write32(smmu500_test_regs::GATS1UR_H, 0);

    ASSERT_EQ(mmio_read32(smmu500_test_regs::GPAR), 0x1001u);
    ASSERT_TRUE(smmu.SMMU_CB_FSR[0][smmu.CB_FSR_PF]);
}

TEST_BENCH(smmu500_test_bench, TransactionAttributesPopulateFullSyndrome)
{
    configure_page_table(0x8000, true);

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_WRITE_COMMAND);
    txn.set_address(0x1234);
    gs::smmu500_transaction_attrs_extension attrs;
    attrs.mid = 0xAB;
    attrs.pid = 0x12;
    attrs.bid = 0x5;
    attrs.privileged = false;
    attrs.instruction = true;
    attrs.non_secure = false;
    attrs.asynchronous = true;
    txn.set_extension(&attrs);

    const auto entry = smmu.smmu500_translate(txn, 0);
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_NONE);

    const uint32_t fsynr0 = mmio_read32(smmu.SMMU_CB_FSYNR0.get_offset());
    ASSERT_TRUE(fsynr0 & smmu500_test_regs::FSYNR0_WNR);
    ASSERT_FALSE(fsynr0 & smmu500_test_regs::FSYNR0_PNU);
    ASSERT_TRUE(fsynr0 & smmu500_test_regs::FSYNR0_IND);
    ASSERT_FALSE(fsynr0 & smmu500_test_regs::FSYNR0_NSATTR);
    ASSERT_TRUE(fsynr0 & smmu500_test_regs::FSYNR0_AFR);
    ASSERT_EQ(mmio_read32(smmu.SMMU_CB_FSYNR1.get_offset()), 0xABu | (0x12u << 8) | (0x5u << 13));

    // The extension is stack-owned; detach it before the payload destructor runs.
    txn.clear_extension(&attrs);
}

TEST_BENCH(smmu500_test_bench, Stage2PageWalkTranslates)
{
    configure_stage2_page_table(0x30000);

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(read_memory32(0x30234), 0xA5A5A5A5u);
}

TEST_BENCH(smmu500_test_bench, Stage2ConcatenatedRootTableTranslates)
{
    configure_stream(smmu500_test_regs::S2CR_TRANSLATION);
    smmu.SMMU_CBAR[0][smmu.CBAR_TYPE] = 0;
    smmu.SMMU_CBA2R[0][smmu.CBA2R_VA64] = 1;
    smmu.SMMU_CB_SCTLR[0][smmu.CB_SCTLR_M] = 1;
    smmu.SMMU_CB_TCR_LPAE[0] = 28 | (5u << 16);
    smmu.SMMU_CB_TTBR0_LOW[0] = 0x65000;
    smmu.SMMU_CB_TTBR0_HIGH[0] = 0;

    // T0SZ=28 and SL0=0 start at L2. The 36-bit IPA selects the fifth
    // concatenated root table from the 4 KiB-aligned TTBR base.
    write_memory64(0x69000, 0x50000 | 0x3);
    write_memory64(0x50008, 0x30000 | 0x3 | (1ULL << 6) | (1ULL << 7) | (1ULL << 10));

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_WRITE_COMMAND);
    txn.set_address(0x100001234ULL);
    const auto entry = smmu.smmu500_translate(txn, 0, false);

    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_EQ(entry.translated_addr, 0x30000u);
}

TEST_BENCH(smmu500_test_bench, GATS12UsesStage2PageWalk)
{
    configure_stage2_page_table(0x30000);
    mmio_write32(smmu500_test_regs::GATS12PR, 0x1000);
    mmio_write32(smmu500_test_regs::GATS12PR_H, 0);

    ASSERT_EQ(mmio_read32(smmu500_test_regs::GPAR), 0x30000u);
}

TEST_BENCH(smmu500_test_bench, GATS12UnprivilegedUsesStage2PageWalk)
{
    configure_stage2_page_table(0x30000);
    mmio_write32(smmu500_test_regs::GATS12UR, 0x1000);
    mmio_write32(smmu500_test_regs::GATS12UR_H, 0);

    ASSERT_EQ(mmio_read32(smmu500_test_regs::GPAR), 0x30000u);
}

TEST_BENCH(smmu500_test_bench, GATS12UnprivilegedWriteUsesStage2PageWalk)
{
    configure_stage2_page_table(0x30000);
    mmio_write32(smmu500_test_regs::GATS12UW, 0x1000);
    mmio_write32(smmu500_test_regs::GATS12UW_H, 0);

    ASSERT_EQ(mmio_read32(smmu500_test_regs::GPAR), 0x30000u);
}

TEST_BENCH(smmu500_test_bench, Stage2FaultRecordsFARAndIPAFAR)
{
    configure_stage2_page_table(0x30000);
    smmu.SMMU_CB_TTBR0_LOW[0] = 0x400000;

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_CB_FAR_LOW[0]), 0x1234u);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_CB_IPAFAR_LOW[0]), 0x1234u);
}

TEST_BENCH(smmu500_test_bench, Stage2HighPhysicalTableIsNotAnOutputAddressFault)
{
    configure_stage2_page_table(0x30000);
    smmu.SMMU_CB_TCR_LPAE[0] = 25; // PS=32-bit output address.
    smmu.SMMU_CB_TTBR0_LOW[0] = 0;
    smmu.SMMU_CB_TTBR0_HIGH[0] = 1;

    // The test memory deliberately has no backing at 4 GiB. Reaching the
    // fetch therefore proves the high table base passed address-size checks.
    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_TRUE(smmu.SMMU_CB_FSR[0][smmu.CB_FSR_EF]);
    ASSERT_FALSE(smmu.SMMU_CB_FSR[0][smmu.CB_FSR_ASF]);
}

TEST_BENCH(smmu500_test_bench, Stage2RejectsOversizedInputAddress)
{
    configure_stage2_page_table(0x30000);
    smmu.SMMU_CBA2R[0][smmu.CBA2R_VA64] = 1;

    // T0SZ=25 accepts a 39-bit IPA. Bit 39 must fault rather than aliasing
    // the valid mapping for 0x1234.
    const auto entry = translate((1ULL << 39) | 0x1234);
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_NONE);
    ASSERT_TRUE(smmu.SMMU_CB_FSR[0][smmu.CB_FSR_ASF]);
}

TEST_BENCH(smmu500_multi_tbu_test_bench, NestedStage1Stage2Translation)
{
    configure_nested_identity_context();

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(read_memory32(0x8234), 0xA5A5A5A5u);
}

TEST_BENCH(smmu500_multi_tbu_test_bench, NestedTranslationIntersectsPermissions)
{
    configure_nested_identity_context();
    write_memory64(0x6008, 0x8000 | 0x3 | (1ULL << 6) | (1ULL << 7) | (1ULL << 10));

    const auto entry = translate();
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RO);

    tlm::tlm_dmi dmi;
    ASSERT_TRUE(tbu_get_dmi(0x1234, dmi));
    ASSERT_TRUE(dmi.is_read_allowed());
    ASSERT_FALSE(dmi.is_write_allowed());
}

TEST_BENCH(smmu500_multi_tbu_test_bench, NestedTranslationUsesSmallestPageSize)
{
    configure_nested_identity_context();

    // The stage-2 L2 block maps the stage-1 tables and their final IPA
    // identity. The nested result must retain the stage-1 4 KiB boundary.
    write_memory64(0x200000, 0x1 | (1ULL << 6) | (1ULL << 7) | (1ULL << 10));

    const auto entry = translate();
    ASSERT_EQ(entry.addr_mask, 0xfffu);
    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(read_memory32(0x8234), 0xA5A5A5A5u);
    ASSERT_EQ(read_memory32(0x1234), 0u);
}

TEST_BENCH(smmu500_multi_tbu_test_bench, NestedTranslationRejectsInvalidStage2ContextBank)
{
    configure_nested_identity_context();
    smmu.SMMU_CBAR[0][smmu.CBAR_S2_CBNDX] = 2;

    const auto entry = translate();
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_NONE);
    ASSERT_TRUE(smmu.SMMU_CB_FSR[0][smmu.CB_FSR_TF]);
}

TEST_BENCH(smmu500_multi_tbu_test_bench, NestedTranslationRejectsOversizedStage1TableIpa)
{
    configure_nested_identity_context();
    smmu.SMMU_CB_TTBR0_HIGH[0] = 0x100; // Bit 40 exceeds the 39-bit stage-2 IPA width.

    const auto entry = translate();
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_NONE);
    ASSERT_TRUE(smmu.SMMU_CB_FSR[0][smmu.CB_FSR_ASF]);
}

TEST_BENCH(smmu500_multi_tbu_test_bench, NestedStage1FaultStopsBeforeStage2)
{
    configure_nested_identity_context();
    write_memory64(0x6008, 0);
    write_memory64(0x201008, 0x9000 | 0x3 | (1ULL << 6) | (1ULL << 7) | (1ULL << 10));

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_TRUE(smmu.SMMU_CB_FSR[0][smmu.CB_FSR_TF]);
    ASSERT_EQ(read_memory32(0x9234), 0u);
}

TEST_BENCH(smmu500_multi_tbu_test_bench, NestedStage1PageWalkUsesStage2ReadPermission)
{
    configure_nested_identity_context();
    write_memory64(0x201020, 0x4000 | 0x3 | (1ULL << 6) | (1ULL << 10));
    write_memory64(0x201028, 0x5000 | 0x3 | (1ULL << 6) | (1ULL << 10));
    write_memory64(0x201030, 0x6000 | 0x3 | (1ULL << 6) | (1ULL << 10));

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(read_memory32(0x8234), 0xA5A5A5A5u);
}

TEST_BENCH(smmu500_multi_tbu_test_bench, NestedStage2FaultDuringStage1WalkSetsPTWF)
{
    configure_nested_identity_context();
    write_memory64(0x201030, 0x6000 | 0x3 | (1ULL << 7) | (1ULL << 10));

    uint32_t value = 0;
    ASSERT_EQ(tbu_read32(0x1234, value), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    const uint32_t fsynr0 = mmio_read32(smmu.SMMU_CB_FSYNR0.get_offset());
    ASSERT_TRUE(fsynr0 & smmu500_test_regs::FSYNR0_PTWF);
    ASSERT_EQ(mmio_read32(smmu.SMMU_CB_IPAFAR_LOW.get_offset()), 0x6008u);
}

TEST_BENCH(smmu500_test_bench, TTBR1HighVAUsesUpperTranslationTable)
{
    configure_ttbr1_page_table(0x8000);
    const uint64_t high_va = 0xFFFFFF8000001234ULL;

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_WRITE_COMMAND);
    txn.set_address(high_va);
    const auto entry = smmu.smmu500_translate(txn, 0);

    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_EQ(entry.translated_addr, 0x8000u);
}

TEST_BENCH(smmu500_test_bench, TBUHighVAUsesTTBR1)
{
    configure_ttbr1_page_table(0x8000);
    constexpr uint64_t high_va = 0xFFFFFF8000001234ULL;

    ASSERT_EQ(tbu_write32(high_va, 0xA5A5A5A5), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(read_memory32(0x8234), 0xA5A5A5A5u);
}

TEST_BENCH(smmu500_test_bench, Stage1AddressSizeFaultRejectsNonCanonicalTTBR0Address)
{
    configure_page_table(0x8000);

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(0x0000010000001234ULL);

    ASSERT_EQ(smmu.smmu500_translate(txn, 0).perm, gs::smmu500<>::IOMMU_NONE);
    ASSERT_TRUE(smmu.SMMU_CB_FSR[0][smmu.CB_FSR_ASF]);
}

TEST_BENCH(smmu500_test_bench, TTBRHighAddressBitsMatchQemuComposition)
{
    configure_page_table(0x8000);
    ASSERT_EQ(gs::smmu500<>::smmu500_ttbr(0x10002, 0x0a81a000), 0x20a81a000ULL);

    ASSERT_EQ(gs::smmu500<>::smmu500_ttbr(0x0, 0x4000), 0x4000ULL);
}

TEST_BENCH(smmu500_test_bench, EPD1DisablesTTBR1Walk)
{
    configure_ttbr1_page_table(0x8000);
    smmu.SMMU_CB_TCR_LPAE[0] |= 1u << 23;

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(0xFFFFFF8000001234ULL);
    ASSERT_EQ(smmu.smmu500_translate(txn, 0).perm, gs::smmu500<>::IOMMU_NONE);
    ASSERT_TRUE(smmu.SMMU_CB_FSR[0][smmu.CB_FSR_TF]);
}

TEST_BENCH(smmu500_test_bench, TTBR1SixteenKGranuleTranslates)
{
    configure_ttbr1_large_granule_page_table(1, 0x200000);

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(0xFFFFFF8000001234ULL);
    const auto entry = smmu.smmu500_translate(txn, 0);
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_EQ(entry.translated_addr, 0x200000u);
    ASSERT_EQ(entry.addr_mask, 0x3fffu);
}

TEST_BENCH(smmu500_test_bench, TTBR1SixtyFourKGranuleTranslates)
{
    configure_ttbr1_large_granule_page_table(3, 0x200000);

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(0xFFFFFF8000001234ULL);
    const auto entry = smmu.smmu500_translate(txn, 0);
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_EQ(entry.translated_addr, 0x200000u);
    ASSERT_EQ(entry.addr_mask, 0xffffu);
}

TEST_BENCH(smmu500_test_bench, Stage1SixteenKGranuleTranslates)
{
    configure_large_granule_page_table(2, 0x200000);

    ASSERT_EQ(tbu_write32(0x1234, 0x16161616), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(read_memory32(0x201234), 0x16161616u);
}

TEST_BENCH(smmu500_test_bench, Stage1SixtyFourKGranuleTranslates)
{
    configure_large_granule_page_table(1, 0x200000);

    ASSERT_EQ(tbu_write32(0x1234, 0x64646464), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(read_memory32(0x201234), 0x64646464u);
}

TEST_BENCH(smmu500_test_bench, Stage1BlockDescriptorTranslates)
{
    configure_block_descriptor(0x40000000);
    const auto entry = translate();

    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_EQ(entry.translated_addr, 0x40000000u);
    ASSERT_EQ(entry.addr_mask, 0x3FFFFFFFu);
}

TEST_BENCH(smmu500_test_bench, EPD0DisablesStage1Walk)
{
    configure_page_table(0x8000);
    smmu.SMMU_CB_TCR_LPAE[0] |= 1u << 7;

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_TRUE(smmu.SMMU_CB_FSR[0][smmu.CB_FSR_TF]);
}

TEST_BENCH(smmu500_test_bench, AccessFlagFaultRejectsPage)
{
    configure_page_table(0x8000, false, false);

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_TRUE(smmu.SMMU_CB_FSR[0][smmu.CB_FSR_AFF]);
}

TEST_BENCH(smmu500_pamax32_test_bench, OutputAddressSizeFaultRejectsPage)
{
    configure_page_table(0x100000000ULL);

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_TRUE(smmu.SMMU_CB_FSR[0][smmu.CB_FSR_ASF]);
}

TEST_BENCH(smmu500_test_bench, ExternalPageTableFetchFaultSetsContextFault)
{
    configure_identity_context();
    smmu.SMMU_CB_SCTLR[0][smmu.CB_SCTLR_M] = 1;
    smmu.SMMU_CB_TCR_LPAE[0] = 25;
    smmu.SMMU_CB_TTBR0_LOW[0] = 0x400000;

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_TRUE(smmu.SMMU_CB_FSR[0][smmu.CB_FSR_EF]);
}

TEST_BENCH(smmu500_test_bench, ExternalFaultRecordsPageWalkSyndrome)
{
    configure_identity_context();
    smmu.SMMU_CB_SCTLR[0][smmu.CB_SCTLR_M] = 1;
    smmu.SMMU_CB_TCR_LPAE[0] = 25;
    smmu.SMMU_CB_TTBR0_LOW[0] = 0x400000;

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_TRUE(smmu.SMMU_CB_FSR[0][smmu.CB_FSR_EF]);
    ASSERT_TRUE(mmio_read32(smmu.SMMU_CB_FSYNR0.get_offset()) & smmu500_test_regs::FSYNR0_PTWF);
    ASSERT_EQ(mmio_read32(smmu.SMMU_CB_FSYNR1.get_offset()), 0u);
}

TEST_BENCH(smmu500_test_bench, MultipleFaultsSetMultiAndPreserveFirstSyndrome)
{
    configure_page_table(0x8000, true);

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    const uint32_t first_syndrome = mmio_read32(smmu.SMMU_CB_FSYNR0.get_offset());
    const uint32_t first_ipafar = mmio_read32(smmu.SMMU_CB_IPAFAR_LOW.get_offset());
    ASSERT_EQ(tbu_write32(0x1abc, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);

    const uint32_t fsr = static_cast<uint32_t>(smmu.SMMU_CB_FSR[0]);
    ASSERT_TRUE(fsr & smmu500_test_regs::CB_FSR_PF);
    ASSERT_TRUE(fsr & smmu500_test_regs::CB_FSR_MULTI);
    ASSERT_EQ(mmio_read32(smmu.SMMU_CB_FSYNR0.get_offset()), first_syndrome);
    ASSERT_EQ(mmio_read32(smmu.SMMU_CB_IPAFAR_LOW.get_offset()), first_ipafar);
}

TEST_BENCH(smmu500_multi_tbu_test_bench, NestedStage2FaultRecordsStage1ContextBank)
{
    configure_nested_stage2_fault_from_cb1();

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    const uint64_t fsynr0 = smmu.SMMU_CB_FSYNR0.get_offset() + 0x1000;
    ASSERT_EQ((mmio_read32(fsynr0) >> 16) & 0x7fu, 1u);
    ASSERT_EQ(mmio_read32(smmu.SMMU_CB_IPAFAR_LOW.get_offset() + 0x1000), 0x8000u);
}

TEST_BENCH(smmu500_context_irq_test_bench, ContextFaultFSRWriteOneToClear)
{
    configure_page_table(0x8000, true);
    smmu.SMMU_CB_SCTLR[0][smmu.CB_SCTLR_CFIE] = 1;

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_TRUE(irq.level);

    mmio_write32(smmu.SMMU_CB_FSR.get_offset(), smmu500_test_regs::CB_FSR_PF);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_CB_FSR[0]), 0u);
    ASSERT_FALSE(irq.level);
}

TEST_BENCH(smmu500_test_bench, TBUIdentityReadWriteAndDebug)
{
    configure_identity_context();
    uint32_t value = 0;

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(tbu_read32(0x1234, value), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(value, 0xA5A5A5A5u);
    ASSERT_EQ(tbu_debug_write32(0x1234, 0x5A5A5A5A), sizeof(uint32_t));
    ASSERT_EQ(tbu_read32(0x1234, value), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(value, 0x5A5A5A5Au);
}

TEST_BENCH(smmu500_test_bench, TBUStage1PageWalkTranslates)
{
    configure_page_table(0x8000);

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(read_memory32(0x8234), 0xA5A5A5A5u);

    uint32_t value = 0;
    ASSERT_EQ(tbu_read32(0x1234, value), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(value, 0xA5A5A5A5u);
    ASSERT_EQ(tbu_debug_read32(0x1234, value), sizeof(uint32_t));
    ASSERT_EQ(value, 0xA5A5A5A5u);
}

TEST_BENCH(smmu500_test_bench, TBUTransportStreamIdDoesNotAffectIOVA)
{
    configure_page_table(0x8000);
    smmu.SMMU_SMR[0][smmu.SMR_ID] = 3;

    ASSERT_EQ(tbu_write32(0x0000000300001234ULL, 0xA5A5A5A5), tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(read_memory32(0x8234), 0xA5A5A5A5u);
}

TEST_BENCH(smmu500_fastrpc_test_bench, FastRpcTransportAddressUsesCsidAndTranslatedIova)
{
    constexpr uint32_t cb = 0x28;
    constexpr uint64_t transport_addr = 0x100010040ULL;
    constexpr uint64_t expected_page = 0xa00003000ULL;

    configure_stream(smmu500_test_regs::S2CR_TRANSLATION, cb);
    smmu.SMMU_SMR[0][smmu.SMR_ID] = 0x21c1;
    smmu.SMMU_SMR[0][smmu.SMR_MASK] = 0;
    smmu.SMMU_SMR[0][smmu.SMR_VALID] = 1;
    smmu.SMMU_CBAR[cb][smmu.CBAR_TYPE] = 1;
    smmu.SMMU_CBA2R[cb][smmu.CBA2R_VA64] = 1;
    smmu.SMMU_CB_SCTLR[cb][smmu.CB_SCTLR_M] = 1;
    smmu.SMMU_CB_TCR_LPAE[cb] = 32;
    smmu.SMMU_CB_TCR2[cb] = 1;
    smmu.SMMU_CB_TTBR0_LOW[cb] = 0x4000;
    smmu.SMMU_CB_TTBR0_HIGH[cb] = 0;

    write_memory64(0x4000, 0x5000 | 0x3);
    write_memory64(0x5000, 0x6000 | 0x3);
    write_memory64(0x6080, expected_page | 0x3 | (1ULL << 6) | (1ULL << 10));

    int resolved_cb = -1;
    ASSERT_EQ(smmu.smmu500_resolve_stream_id(0x21c0, transport_addr, &resolved_cb), 0x21c1u);
    ASSERT_EQ(resolved_cb, static_cast<int>(cb));

    tlm::tlm_generic_payload txn;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(transport_addr);
    const auto entry = smmu.smmu500_translate(txn, 0x21c0, true);
    ASSERT_EQ(entry.perm, gs::smmu500<>::IOMMU_RW);
    ASSERT_EQ(entry.translated_addr, expected_page);
    ASSERT_EQ((entry.translated_addr & ~entry.addr_mask) | (transport_addr & entry.addr_mask), 0xa00003040ULL);
}

TEST_BENCH(smmu500_test_bench, DirectTranslationPreservesIOVA)
{
    configure_page_table(0x8000);

    const auto entry = translate(0x1234);
    ASSERT_EQ(entry.iova, 0x1234u);
    ASSERT_EQ(entry.translated_addr, 0x8000u);
}

TEST_BENCH(smmu500_test_bench, TBUStage1PermissionFaultRejectsWrite)
{
    configure_page_table(0x8000, true);

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_TRUE(smmu.SMMU_CB_FSR[0][smmu.CB_FSR_PF]);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_SGFSR), 0u);
}

TEST_BENCH(smmu500_test_bench, TBUDMIStage1MappingUsesVirtualPageRange)
{
    configure_page_table(0x8000);
    tlm::tlm_dmi dmi;

    ASSERT_TRUE(tbu_get_dmi(0x1234, dmi));
    ASSERT_EQ(dmi.get_start_address(), 0x1000u);
    ASSERT_EQ(dmi.get_end_address(), 0x1FFFu);
    ASSERT_TRUE(dmi.is_read_allowed());
    ASSERT_TRUE(dmi.is_write_allowed());
}

TEST_BENCH(smmu500_test_bench, TBUDMIAllowsSecurityDependentGlobalControls)
{
    configure_page_table(0x8000);
    smmu.SMMU_NSCR0 = smmu500_test_regs::GFRE;

    tlm::tlm_dmi dmi;
    ASSERT_TRUE(tbu_get_dmi(0x1234, dmi));
}

TEST_BENCH(smmu500_test_bench, TBUDMIAllowsSecurityAnnotatedRequestsWithEquivalentGlobalControls)
{
    configure_page_table(0x8000);
    tlm::tlm_dmi dmi;
    ASSERT_TRUE(tbu_get_dmi_with_security(0x1234, dmi, false));
    ASSERT_TRUE(tbu_get_dmi_with_security(0x1234, dmi, true));
}

TEST_BENCH(smmu500_test_bench, TBUDMIAllowsSecurityAnnotatedRequestsWithDifferentGlobalControls)
{
    configure_page_table(0x8000);
    smmu.SMMU_NSCR0 = smmu500_test_regs::GFRE;
    tlm::tlm_dmi dmi;
    ASSERT_TRUE(tbu_get_dmi_with_security(0x1234, dmi, false));
    ASSERT_TRUE(tbu_get_dmi_with_security(0x1234, dmi, true));
}

TEST_BENCH(smmu500_test_bench, TBUDMIAllowsSecurityAnnotatedTransportWithEquivalentGlobalControls)
{
    configure_page_table(0x8000);

    uint32_t value = 0;
    tlm::tlm_generic_payload txn;
    gs::QemuMemTxSecureTlmExtension security;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(0x1234);
    txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
    txn.set_data_length(sizeof(value));
    txn.set_streaming_width(sizeof(value));
    txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    security.value = true;
    txn.set_extension(&security);
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    tbu_initiator->b_transport(txn, delay);
    txn.clear_extension(&security);

    ASSERT_EQ(txn.get_response_status(), tlm::TLM_OK_RESPONSE);
    ASSERT_TRUE(txn.is_dmi_allowed());
}

TEST_BENCH(smmu500_test_bench, TBUDMIPreservesSecurityAnnotatedTransportWhenControlsDiffer)
{
    configure_page_table(0x8000);
    smmu.SMMU_NSCR0 = smmu500_test_regs::GFRE;

    uint32_t value = 0;
    tlm::tlm_generic_payload txn;
    gs::QemuMemTxSecureTlmExtension security;
    txn.set_command(tlm::TLM_READ_COMMAND);
    txn.set_address(0x1234);
    txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
    txn.set_data_length(sizeof(value));
    txn.set_streaming_width(sizeof(value));
    txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    security.value = true;
    txn.set_extension(&security);
    sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
    tbu_initiator->b_transport(txn, delay);
    txn.clear_extension(&security);

    ASSERT_EQ(txn.get_response_status(), tlm::TLM_OK_RESPONSE);
    ASSERT_TRUE(txn.is_dmi_allowed());
}

TEST_BENCH(smmu500_test_bench, GlobalControlWriteInvalidatesTbuDMI)
{
    configure_page_table(0x8000);
    tlm::tlm_dmi dmi;
    ASSERT_TRUE(tbu_get_dmi(0x1234, dmi));
    dmi_invalidations.clear();

    mmio_write32(smmu500_test_regs::NSCR0, smmu500_test_regs::GFRE);
    ASSERT_EQ(dmi_invalidations.size(), 1u);
    ASSERT_EQ(dmi_invalidations[0].first, 0x1000u);
    ASSERT_EQ(dmi_invalidations[0].second, 0x1fffu);
}

TEST_BENCH(smmu500_multi_tbu_test_bench, TLBIASIDInvalidatesAllMatchingTBUViews)
{
    configure_identity_context(1);
    tlm::tlm_dmi first_dmi;
    tlm::tlm_dmi second_dmi;

    ASSERT_TRUE(tbu_get_dmi(0x1234, first_dmi));
    ASSERT_TRUE(tbu_second_get_dmi(0x1234, second_dmi));
    dmi_invalidations.clear();

    mmio_write32(smmu.SMMU_CB_TLBIASID.get_offset() + 0x1000, 0x4321);

    ASSERT_EQ(dmi_invalidations.size(), 2u);
    ASSERT_EQ(dmi_invalidations[0].first, first_dmi.get_start_address());
    ASSERT_EQ(dmi_invalidations[0].second, first_dmi.get_end_address());
    ASSERT_EQ(dmi_invalidations[1].first, second_dmi.get_start_address());
    ASSERT_EQ(dmi_invalidations[1].second, second_dmi.get_end_address());
}

TEST_BENCH(smmu500_test_bench, TBUFaultRejectsTransportAndDebug)
{
    configure_stream(smmu500_test_regs::S2CR_FAULT);
    smmu.SMMU_NSCR0 = smmu500_test_regs::GFRE;

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_EQ(tbu_debug_write32(0x1234, 0), 0u);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), smmu500_test_regs::ICF);
}

TEST_BENCH(smmu500_test_bench, DebugGlobalFaultDoesNotRecordStatus)
{
    configure_stream(smmu500_test_regs::S2CR_FAULT);
    smmu.SMMU_NSCR0 = smmu500_test_regs::GFRE;

    ASSERT_EQ(tbu_debug_write32(0x1234, 0), 0u);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), 0u);
}

TEST_BENCH(smmu500_test_bench, DebugTranslationFaultDoesNotRecordContextFault)
{
    configure_page_table(0x8000, false, false);

    uint32_t value = 0;
    ASSERT_EQ(tbu_debug_read32(0x1234, value), 0u);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_CB_FSR[0]), 0u);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_NSGFSR), 0u);
}

TEST_BENCH(smmu500_context_irq_test_bench, ContextFaultIRQTracksFSR)
{
    configure_page_table(0x8000, true);
    smmu.SMMU_CB_SCTLR[0][smmu.CB_SCTLR_CFIE] = 1;

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_TRUE(irq.level);
    ASSERT_EQ(irq.rising_edges, 1u);
}

TEST_BENCH(smmu500_context_irq_test_bench, ContextFaultIRQStaysLowWhenDisabled)
{
    configure_page_table(0x8000, true);

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_FALSE(irq.level);
    ASSERT_EQ(irq.rising_edges, 0u);
}

TEST_BENCH(smmu500_context_irq_test_bench, ContextFaultIRQTracksCFIEWrites)
{
    configure_page_table(0x8000, true);

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_FALSE(irq.level);

    mmio_write32(smmu.SMMU_CB_SCTLR.get_offset(), 1u << 6);
    ASSERT_TRUE(irq.level);

    mmio_write32(smmu.SMMU_CB_SCTLR.get_offset(), 0);
    ASSERT_FALSE(irq.level);
}

TEST_BENCH(smmu500_context_irq_test_bench, ResetDeassertsContextFaultIRQ)
{
    configure_page_table(0x8000, true);
    smmu.SMMU_CB_SCTLR[0][smmu.CB_SCTLR_CFIE] = 1;

    ASSERT_EQ(tbu_write32(0x1234, 0xA5A5A5A5), tlm::TLM_ADDRESS_ERROR_RESPONSE);
    ASSERT_TRUE(irq.level);

    reset_drv->write(true);
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_CB_FSR[0]), 0u);
    ASSERT_FALSE(irq.level);
    reset_drv->write(false);
}

TEST_BENCH(smmu500_test_bench, TBUDMIAndTLBIALLInvalidate)
{
    configure_identity_context();
    tlm::tlm_dmi dmi;

    ASSERT_TRUE(tbu_get_dmi(0x1234, dmi));
    ASSERT_EQ(dmi.get_start_address(), 0x1000u);
    ASSERT_EQ(dmi.get_end_address(), 0x1FFFu);
    ASSERT_TRUE(dmi.is_read_allowed());
    ASSERT_TRUE(dmi.is_write_allowed());

    mmio_write32(smmu.SMMU_CB_TLBIALL.get_offset(), 0);
    ASSERT_EQ(dmi_invalidations.size(), 1u);
    ASSERT_EQ(dmi_invalidations[0].first, 0x1000u);
    ASSERT_EQ(dmi_invalidations[0].second, 0x1FFFu);

    dmi_invalidations.clear();
    ASSERT_TRUE(tbu_get_dmi(0x1234, dmi));
    reset_drv->write(true);
    ASSERT_EQ(dmi_invalidations.size(), 1u);
    ASSERT_EQ(dmi_invalidations[0].first, 0x1000u);
    ASSERT_EQ(dmi_invalidations[0].second, 0x1FFFu);
    reset_drv->write(false);
}

TEST_BENCH(smmu500_test_bench, TBURepeatedResetInvalidatesNewDMI)
{
    tlm::tlm_dmi dmi;

    for (unsigned int reset = 0; reset < 2; ++reset) {
        configure_identity_context();
        ASSERT_TRUE(tbu_get_dmi(0x1234, dmi));

        dmi_invalidations.clear();
        reset_drv->write(true);
        ASSERT_EQ(dmi_invalidations.size(), 1u);
        ASSERT_EQ(dmi_invalidations[0].first, 0x1000u);
        ASSERT_EQ(dmi_invalidations[0].second, 0x1FFFu);
        reset_drv->write(false);
    }
}

TEST_BENCH(smmu500_test_bench, GlobalTLBIALLInvalidatesDMIAndReadsAsZero)
{
    configure_identity_context();
    tlm::tlm_dmi dmi;

    ASSERT_TRUE(tbu_get_dmi(0x1234, dmi));
    dmi_invalidations.clear();

    mmio_write32(smmu500_test_regs::STLBIALL, 0xffffffffu);

    ASSERT_EQ(dmi_invalidations.size(), 1u);
    ASSERT_EQ(dmi_invalidations[0].first, 0x1000u);
    ASSERT_EQ(dmi_invalidations[0].second, 0x1FFFu);
    ASSERT_EQ(mmio_read32(smmu500_test_regs::STLBIALL), 0u);
}

TEST_BENCH(smmu500_test_bench, NonSecureTLBIALLNSNHInvalidatesDMI)
{
    configure_identity_context();
    tlm::tlm_dmi dmi;

    ASSERT_TRUE(tbu_get_dmi(0x1234, dmi));
    dmi_invalidations.clear();

    ASSERT_EQ(mmio_write32_with_security(smmu500_test_regs::TLBIALLNSNH, 0, false),
              tlm::TLM_OK_RESPONSE);
    ASSERT_EQ(dmi_invalidations.size(), 1u);
    ASSERT_EQ(dmi_invalidations[0].first, 0x1000u);
    ASSERT_EQ(dmi_invalidations[0].second, 0x1fffu);
}

TEST_BENCH(smmu500_test_bench, PerContextVATLBIInvalidatesDMIAndReadsAsZero)
{
    configure_identity_context();
    tlm::tlm_dmi dmi;

    ASSERT_TRUE(tbu_get_dmi(0x1234, dmi));
    dmi_invalidations.clear();

    mmio_write32(smmu.SMMU_CB_TLBIVA_LOW.get_offset(), 0x1234u);

    ASSERT_EQ(dmi_invalidations.size(), 1u);
    ASSERT_EQ(dmi_invalidations[0].first, 0x1000u);
    ASSERT_EQ(dmi_invalidations[0].second, 0x1FFFu);
    ASSERT_EQ(mmio_read32(smmu.SMMU_CB_TLBIVA_LOW.get_offset()), 0u);
}

TEST_BENCH(smmu500_multi_tbu_test_bench, TLBIVMIDInvalidatesMatchingContextBanks)
{
    configure_identity_context(1);
    smmu.SMMU_CBAR[0][smmu.CBAR_VMID] = 7;
    smmu.SMMU_CBAR[1][smmu.CBAR_VMID] = 9;
    tlm::tlm_dmi first_dmi;
    tlm::tlm_dmi second_dmi;

    ASSERT_TRUE(tbu_get_dmi(0x1234, first_dmi));
    ASSERT_TRUE(tbu_second_get_dmi(0x1234, second_dmi));
    dmi_invalidations.clear();

    mmio_write32(smmu500_test_regs::TLBIVMID, 9u);

    ASSERT_EQ(dmi_invalidations.size(), 2u);
    ASSERT_EQ(dmi_invalidations[0].first, first_dmi.get_start_address());
    ASSERT_EQ(dmi_invalidations[0].second, first_dmi.get_end_address());
    ASSERT_EQ(dmi_invalidations[1].first, second_dmi.get_start_address());
    ASSERT_EQ(dmi_invalidations[1].second, second_dmi.get_end_address());
    ASSERT_EQ(mmio_read32(smmu500_test_regs::TLBIVMID), 0u);
}

TEST_BENCH(smmu500_test_bench, TBUFaultDMIInvalidatedByTLBIALL)
{
    configure_page_table(0x8000, false, false);
    tlm::tlm_dmi dmi;

    ASSERT_FALSE(tbu_get_dmi(0x1234, dmi));
    ASSERT_EQ(static_cast<uint32_t>(smmu.SMMU_CB_FSR[0]), 0u);
    ASSERT_TRUE(dmi_invalidations.empty());

    mmio_write32(smmu.SMMU_CB_TLBIALL.get_offset(), 0);
    ASSERT_EQ(dmi_invalidations.size(), 1u);
    ASSERT_EQ(dmi_invalidations[0].first, 0x1000u);
    ASSERT_EQ(dmi_invalidations[0].second, 0x1FFFu);
}

TEST_BENCH(smmu500_irq_test_bench, NonSecureGlobalFaultIRQTracksStatus)
{
    configure_stream(smmu500_test_regs::S2CR_FAULT);
    smmu.SMMU_NSCR0 = smmu500_test_regs::GFRE | smmu500_test_regs::GFIE;

    translate();
    ASSERT_TRUE(irq.level);
    ASSERT_EQ(irq.rising_edges, 1u);

    mmio_write32(smmu500_test_regs::NSGFSR, smmu500_test_regs::ICF);
    ASSERT_FALSE(irq.level);
}

TEST_BENCH(smmu500_irq_test_bench, NonSecureGlobalFaultIRQRespondsToNSCR0Writes)
{
    configure_stream(smmu500_test_regs::S2CR_FAULT);
    smmu.SMMU_NSCR0 = smmu500_test_regs::GFRE;
    translate();
    ASSERT_FALSE(irq.level);

    mmio_write32(smmu500_test_regs::NSCR0, smmu500_test_regs::GFRE | smmu500_test_regs::GFIE);
    ASSERT_TRUE(irq.level);
    ASSERT_EQ(irq.rising_edges, 1u);
}

TEST_BENCH(smmu500_irq_test_bench, GlobalIRQRemainsAssertedWhileEitherSecurityBankIsPending)
{
    configure_stream(smmu500_test_regs::S2CR_FAULT);
    smmu.SMMU_SCR0 = smmu500_test_regs::GFRE | smmu500_test_regs::GFIE;
    smmu.SMMU_NSCR0 = smmu500_test_regs::GFRE | smmu500_test_regs::GFIE;

    tlm::tlm_generic_payload secure_txn;
    secure_txn.set_command(tlm::TLM_READ_COMMAND);
    secure_txn.set_address(0x1234);
    gs::QemuMemTxSecureTlmExtension security;
    security.value = true;
    secure_txn.set_extension(&security);
    ASSERT_EQ(smmu.smmu500_translate(secure_txn, 0).perm, gs::smmu500<>::IOMMU_NONE);
    secure_txn.clear_extension(&security);
    ASSERT_TRUE(irq.level);

    ASSERT_EQ(translate().perm, gs::smmu500<>::IOMMU_NONE);
    ASSERT_TRUE(irq.level);
    mmio_write32(smmu500_test_regs::SGFSR, smmu500_test_regs::ICF);
    ASSERT_TRUE(irq.level);
    mmio_write32(smmu500_test_regs::NSGFSR, smmu500_test_regs::ICF);
    ASSERT_FALSE(irq.level);
}

int sc_main(int argc, char** argv)
{
    gs::ConfigurableBroker broker{};
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
