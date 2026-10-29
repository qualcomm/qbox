/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef smmu500_gen_H
#define smmu500_gen_H

#include <reg_model_maker/reg_model_maker.h>
#include <cstdint>
#include <string>

namespace gs {
class smmu500_gen
{
private:
    std::string m_name;

public:
    static constexpr uint32_t CB_BANK_COUNT = 128;
    using reg_bank = gs::gs_register<gs::gs_bank_of<uint32_t>>;

    gs::gs_register<uint32_t> SMMU_SCR0;
    gs::gs_field<uint32_t> SCR0_CLIENTPD;
    gs::gs_field<uint32_t> SCR0_GFRE;
    gs::gs_field<uint32_t> SCR0_GFIE;
    gs::gs_field<uint32_t> SCR0_GCFGFRE;
    gs::gs_field<uint32_t> SCR0_GCFGFIE;
    gs::gs_field<uint32_t> SCR0_USFCFG;
    gs::gs_field<uint32_t> SCR0_SMCFCFG;
    gs::gs_register<uint32_t> SMMU_NSCR0;
    gs::gs_register<uint32_t> SMMU_SCR1;
    gs::gs_field<uint32_t> SCR1_NSNUMCBO;
    gs::gs_field<uint32_t> SCR1_NSNUMSMRGO;
    gs::gs_register<uint32_t> SMMU_SACR;
    gs::gs_register<uint32_t> SMMU_SIDR0;
    gs::gs_field<uint32_t> SIDR0_NUMSMRG;
    gs::gs_field<uint32_t> SIDR0_ATOSNS;
    gs::gs_register<uint32_t> SMMU_SIDR1;
    gs::gs_field<uint32_t> SIDR1_NUMCB;
    gs::gs_field<uint32_t> SIDR1_NUMPAGENDXB;
    gs::gs_register<uint32_t> SMMU_SIDR2;
    gs::gs_register<uint32_t> SMMU_SIDR7;
    gs::gs_register<uint32_t> SMMU_SGFSR;
    gs::gs_field<uint32_t> SGFSR_ICF;
    gs::gs_field<uint32_t> SGFSR_USF;
    gs::gs_field<uint32_t> SGFSR_SMCF;
    gs::gs_register<uint32_t> SMMU_NSGFSR;
    gs::gs_register<uint32_t> SMMU_STLBIALL;
    gs::gs_register<uint32_t> SMMU_TLBIVMID;
    gs::gs_register<uint32_t> SMMU_TLBIALLNSNH;
    gs::gs_register<uint32_t> SMMU_TLBIALLH;
    gs::gs_register<uint32_t> SMMU_TLBIVAH_LOW;
    gs::gs_register<uint32_t> SMMU_STLBIVALM_LOW;
    gs::gs_register<uint32_t> SMMU_STLBIVALM_HIGH;
    gs::gs_register<uint32_t> SMMU_STLBIVAM_LOW;
    gs::gs_register<uint32_t> SMMU_STLBIVAM_HIGH;
    gs::gs_register<uint32_t> SMMU_TLBIVALH64_LOW;
    gs::gs_register<uint32_t> SMMU_TLBIVALH64_HIGH;
    gs::gs_register<uint32_t> SMMU_TLBIVMIDS1;
    gs::gs_register<uint32_t> SMMU_STLBIALLM;
    gs::gs_register<uint32_t> SMMU_TLBIVAH64_LOW;
    gs::gs_register<uint32_t> SMMU_TLBIVAH64_HIGH;
    gs::gs_register<uint32_t> SMMU_SMR;
    gs::gs_field<uint32_t> SMR_ID;
    gs::gs_field<uint32_t> SMR_MASK;
    gs::gs_field<uint32_t> SMR_VALID;
    gs::gs_register<uint32_t> SMMU_S2CR;
    gs::gs_field<uint32_t> S2CR_CBNDX_VMID;
    gs::gs_field<uint32_t> S2CR_TYPE;
    gs::gs_register<uint32_t> SMMU_CBAR;
    gs::gs_field<uint32_t> CBAR_VMID;
    gs::gs_field<uint32_t> CBAR_S2_CBNDX;
    gs::gs_field<uint32_t> CBAR_TYPE;
    gs::gs_register<uint32_t> SMMU_CBA2R;
    gs::gs_field<uint32_t> CBA2R_VA64;
    gs::gs_register<uint32_t> SMMU_GATS1PR;
    gs::gs_register<uint32_t> SMMU_GATS1PR_H;
    gs::gs_register<uint32_t> SMMU_GATS1PW;
    gs::gs_register<uint32_t> SMMU_GATS1PW_H;
    gs::gs_register<uint32_t> SMMU_GATS1UR;
    gs::gs_register<uint32_t> SMMU_GATS1UR_H;
    gs::gs_register<uint32_t> SMMU_GATS1UW;
    gs::gs_register<uint32_t> SMMU_GATS1UW_H;
    gs::gs_register<uint32_t> SMMU_GATS12PR;
    gs::gs_register<uint32_t> SMMU_GATS12PR_H;
    gs::gs_register<uint32_t> SMMU_GATS12PW;
    gs::gs_register<uint32_t> SMMU_GATS12PW_H;
    gs::gs_register<uint32_t> SMMU_GATS12UR;
    gs::gs_register<uint32_t> SMMU_GATS12UR_H;
    gs::gs_register<uint32_t> SMMU_GATS12UW;
    gs::gs_register<uint32_t> SMMU_GATS12UW_H;
    gs::gs_register<uint32_t> SMMU_GPAR;
    gs::gs_register<uint32_t> SMMU_GPAR_H;
    gs::gs_register<uint32_t> SMMU_TBU_PWR_STATUS;
    reg_bank SMMU_CB_SCTLR;
    reg_bank SMMU_CB_ACTLR;
    reg_bank SMMU_CB_RESUME;
    reg_bank SMMU_CB_TCR2;
    reg_bank SMMU_CB_TTBR0_LOW;
    reg_bank SMMU_CB_TTBR0_HIGH;
    reg_bank SMMU_CB_TTBR1_LOW;
    reg_bank SMMU_CB_TTBR1_HIGH;
    reg_bank SMMU_CB_TCR_LPAE;
    reg_bank SMMU_CB_CONTEXTIDR;
    reg_bank SMMU_CB_PRRR_MAIR0;
    reg_bank SMMU_CB_NMRR_MAIR1;
    reg_bank SMMU_CB_FSR;
    reg_bank SMMU_CB_FSRRESTORE;
    reg_bank SMMU_CB_FAR_LOW;
    reg_bank SMMU_CB_FAR_HIGH;
    reg_bank SMMU_CB_FSYNR0;
    reg_bank SMMU_CB_FSYNR1;
    reg_bank SMMU_CB_IPAFAR_LOW;
    reg_bank SMMU_CB_IPAFAR_HIGH;
    reg_bank SMMU_CB_TLBIVA_LOW;
    reg_bank SMMU_CB_TLBIVA_HIGH;
    reg_bank SMMU_CB_TLBIVAA_LOW;
    reg_bank SMMU_CB_TLBIVAA_HIGH;
    reg_bank SMMU_CB_TLBIASID;
    reg_bank SMMU_CB_TLBIALL;
    reg_bank SMMU_CB_TLBIVAL_LOW;
    reg_bank SMMU_CB_TLBIVAL_HIGH;
    reg_bank SMMU_CB_TLBIVAAL_LOW;
    reg_bank SMMU_CB_TLBIVAAL_HIGH;
    reg_bank SMMU_CB_TLBIIPAS2_LOW;
    reg_bank SMMU_CB_TLBIIPAS2_HIGH;
    reg_bank SMMU_CB_TLBIIPAS2L_LOW;
    reg_bank SMMU_CB_TLBIIPAS2L_HIGH;
    reg_bank SMMU_CB_TLBSYNC;
    reg_bank SMMU_CB_TLBSTATUS;

    /* CB register fields (parent reg is a placeholder for bit_start/bit_length only) */
    gs::gs_field<uint32_t> CB_SCTLR_M;
    gs::gs_field<uint32_t> CB_SCTLR_CFIE;
    gs::gs_field<uint32_t> CB_FSR_TF;
    gs::gs_field<uint32_t> CB_FSR_AFF;
    gs::gs_field<uint32_t> CB_FSR_PF;
    gs::gs_field<uint32_t> CB_FSR_EF;
    gs::gs_field<uint32_t> CB_FSR_ASF;
    gs::gs_field<uint32_t> CB_FSR_MULTI;
    gs::gs_field<uint32_t> CB_FSYNR0_PLVL;
    gs::gs_field<uint32_t> CB_FSYNR0_WNR;
    gs::gs_field<uint32_t> CB_FSYNR0_PNU;
    gs::gs_field<uint32_t> CB_FSYNR0_IND;
    gs::gs_field<uint32_t> CB_FSYNR0_NSATTR;
    gs::gs_field<uint32_t> CB_FSYNR0_ATOF;
    gs::gs_field<uint32_t> CB_FSYNR0_PTWF;
    gs::gs_field<uint32_t> CB_FSYNR0_AFR;
    gs::gs_field<uint32_t> CB_FSYNR0_S1CBNDX;
    gs::gs_field<uint32_t> CB_FSYNR1_MID;
    gs::gs_field<uint32_t> CB_FSYNR1_PID;
    gs::gs_field<uint32_t> CB_FSYNR1_BID;

    smmu500_gen()
        : m_name("smmu500_gen")
        , SMMU_SCR0("SMMU_SCR0", "smmu500.SMMU_SCR0", 0x0, 1)
        , SCR0_CLIENTPD(SMMU_SCR0, SMMU_SCR0.get_regname() + ".CLIENTPD", 0, 1)
        , SCR0_GFRE(SMMU_SCR0, SMMU_SCR0.get_regname() + ".GFRE", 1, 1)
        , SCR0_GFIE(SMMU_SCR0, SMMU_SCR0.get_regname() + ".GFIE", 2, 1)
        , SCR0_GCFGFRE(SMMU_SCR0, SMMU_SCR0.get_regname() + ".GCFGFRE", 4, 1)
        , SCR0_GCFGFIE(SMMU_SCR0, SMMU_SCR0.get_regname() + ".GCFGFIE", 5, 1)
        , SCR0_USFCFG(SMMU_SCR0, SMMU_SCR0.get_regname() + ".USFCFG", 10, 1)
        , SCR0_SMCFCFG(SMMU_SCR0, SMMU_SCR0.get_regname() + ".SMCFCFG", 21, 1)
        , SMMU_NSCR0("SMMU_NSCR0", "smmu500.SMMU_NSCR0", 0x400, 1)
        , SMMU_SCR1("SMMU_SCR1", "smmu500.SMMU_SCR1", 0x4, 1)
        , SCR1_NSNUMCBO(SMMU_SCR1, SMMU_SCR1.get_regname() + ".NSNUMCBO", 0, 8)
        , SCR1_NSNUMSMRGO(SMMU_SCR1, SMMU_SCR1.get_regname() + ".NSNUMSMRGO", 8, 8)
        , SMMU_SACR("SMMU_SACR", "smmu500.SMMU_SACR", 0x10, 1)
        , SMMU_SIDR0("SMMU_SIDR0", "smmu500.SMMU_SIDR0", 0x20, 1)
        , SIDR0_NUMSMRG(SMMU_SIDR0, SMMU_SIDR0.get_regname() + ".NUMSMRG", 0, 8)
        , SIDR0_ATOSNS(SMMU_SIDR0, SMMU_SIDR0.get_regname() + ".ATOSNS", 26, 1)
        , SMMU_SIDR1("SMMU_SIDR1", "smmu500.SMMU_SIDR1", 0x24, 1)
        , SIDR1_NUMCB(SMMU_SIDR1, SMMU_SIDR1.get_regname() + ".NUMCB", 0, 8)
        , SIDR1_NUMPAGENDXB(SMMU_SIDR1, SMMU_SIDR1.get_regname() + ".NUMPAGENDXB", 28, 3)
        , SMMU_SIDR2("SMMU_SIDR2", "smmu500.SMMU_SIDR2", 0x28, 1)
        , SMMU_SIDR7("SMMU_SIDR7", "smmu500.SMMU_SIDR7", 0x3c, 1)
        , SMMU_SGFSR("SMMU_SGFSR", "smmu500.SMMU_SGFSR", 0x48, 1)
        , SGFSR_ICF(SMMU_SGFSR, SMMU_SGFSR.get_regname() + ".ICF", 0, 1)
        , SGFSR_USF(SMMU_SGFSR, SMMU_SGFSR.get_regname() + ".USF", 1, 1)
        , SGFSR_SMCF(SMMU_SGFSR, SMMU_SGFSR.get_regname() + ".SMCF", 2, 1)
        , SMMU_NSGFSR("SMMU_NSGFSR", "smmu500.SMMU_NSGFSR", 0x448, 1)
        , SMMU_STLBIALL("SMMU_STLBIALL", "smmu500.SMMU_STLBIALL", 0x60, 1)
        , SMMU_TLBIVMID("SMMU_TLBIVMID", "smmu500.SMMU_TLBIVMID", 0x64, 1)
        , SMMU_TLBIALLNSNH("SMMU_TLBIALLNSNH", "smmu500.SMMU_TLBIALLNSNH", 0x68, 1)
        , SMMU_TLBIALLH("SMMU_TLBIALLH", "smmu500.SMMU_TLBIALLH", 0x6c, 1)
        , SMMU_TLBIVAH_LOW("SMMU_TLBIVAH_LOW", "smmu500.SMMU_TLBIVAH_LOW", 0x78, 1)
        , SMMU_STLBIVALM_LOW("SMMU_STLBIVALM_LOW", "smmu500.SMMU_STLBIVALM_LOW", 0xa0, 1)
        , SMMU_STLBIVALM_HIGH("SMMU_STLBIVALM_HIGH", "smmu500.SMMU_STLBIVALM_HIGH", 0xa4, 1)
        , SMMU_STLBIVAM_LOW("SMMU_STLBIVAM_LOW", "smmu500.SMMU_STLBIVAM_LOW", 0xa8, 1)
        , SMMU_STLBIVAM_HIGH("SMMU_STLBIVAM_HIGH", "smmu500.SMMU_STLBIVAM_HIGH", 0xac, 1)
        , SMMU_TLBIVALH64_LOW("SMMU_TLBIVALH64_LOW", "smmu500.SMMU_TLBIVALH64_LOW", 0xb0, 1)
        , SMMU_TLBIVALH64_HIGH("SMMU_TLBIVALH64_HIGH", "smmu500.SMMU_TLBIVALH64_HIGH", 0xb4, 1)
        , SMMU_TLBIVMIDS1("SMMU_TLBIVMIDS1", "smmu500.SMMU_TLBIVMIDS1", 0xb8, 1)
        , SMMU_STLBIALLM("SMMU_STLBIALLM", "smmu500.SMMU_STLBIALLM", 0xbc, 1)
        , SMMU_TLBIVAH64_LOW("SMMU_TLBIVAH64_LOW", "smmu500.SMMU_TLBIVAH64_LOW", 0xc0, 1)
        , SMMU_TLBIVAH64_HIGH("SMMU_TLBIVAH64_HIGH", "smmu500.SMMU_TLBIVAH64_HIGH", 0xc4, 1)
        , SMMU_SMR("SMMU_SMR", "smmu500.SMMU_SMR", 0x800, 224)
        , SMR_ID(SMMU_SMR, SMMU_SMR.get_regname() + ".ID", 0, 15)
        , SMR_MASK(SMMU_SMR, SMMU_SMR.get_regname() + ".MASK", 16, 15)
        , SMR_VALID(SMMU_SMR, SMMU_SMR.get_regname() + ".VALID", 31, 1)
        , SMMU_S2CR("SMMU_S2CR", "smmu500.SMMU_S2CR", 0xc00, 224)
        , S2CR_CBNDX_VMID(SMMU_S2CR, SMMU_S2CR.get_regname() + ".CBNDX_VMID", 0, 8)
        , S2CR_TYPE(SMMU_S2CR, SMMU_S2CR.get_regname() + ".TYPE", 16, 2)
        , SMMU_CBAR("SMMU_CBAR", "smmu500.SMMU_CBAR", 0x1000, 128)
        , CBAR_VMID(SMMU_CBAR, SMMU_CBAR.get_regname() + ".VMID", 0, 8)
        , CBAR_S2_CBNDX(SMMU_CBAR, SMMU_CBAR.get_regname() + ".S2_CBNDX", 8, 8)
        , CBAR_TYPE(SMMU_CBAR, SMMU_CBAR.get_regname() + ".TYPE", 16, 2)
        , SMMU_CBA2R("SMMU_CBA2R", "smmu500.SMMU_CBA2R", 0x1800, 128)
        , CBA2R_VA64(SMMU_CBA2R, SMMU_CBA2R.get_regname() + ".VA64", 0, 1)
        , SMMU_GATS1PR("SMMU_GATS1PR", "smmu500.SMMU_GATS1PR", 0x110, 1)
        , SMMU_GATS1PR_H("SMMU_GATS1PR_H", "smmu500.SMMU_GATS1PR_H", 0x114, 1)
        , SMMU_GATS1PW("SMMU_GATS1PW", "smmu500.SMMU_GATS1PW", 0x118, 1)
        , SMMU_GATS1PW_H("SMMU_GATS1PW_H", "smmu500.SMMU_GATS1PW_H", 0x11C, 1)
        , SMMU_GATS1UR("SMMU_GATS1UR", "smmu500.SMMU_GATS1UR", 0x100, 1)
        , SMMU_GATS1UR_H("SMMU_GATS1UR_H", "smmu500.SMMU_GATS1UR_H", 0x104, 1)
        , SMMU_GATS1UW("SMMU_GATS1UW", "smmu500.SMMU_GATS1UW", 0x108, 1)
        , SMMU_GATS1UW_H("SMMU_GATS1UW_H", "smmu500.SMMU_GATS1UW_H", 0x10C, 1)
        , SMMU_GATS12PR("SMMU_GATS12PR", "smmu500.SMMU_GATS12PR", 0x130, 1)
        , SMMU_GATS12PR_H("SMMU_GATS12PR_H", "smmu500.SMMU_GATS12PR_H", 0x134, 1)
        , SMMU_GATS12PW("SMMU_GATS12PW", "smmu500.SMMU_GATS12PW", 0x138, 1)
        , SMMU_GATS12PW_H("SMMU_GATS12PW_H", "smmu500.SMMU_GATS12PW_H", 0x13C, 1)
        , SMMU_GATS12UR("SMMU_GATS12UR", "smmu500.SMMU_GATS12UR", 0x120, 1)
        , SMMU_GATS12UR_H("SMMU_GATS12UR_H", "smmu500.SMMU_GATS12UR_H", 0x124, 1)
        , SMMU_GATS12UW("SMMU_GATS12UW", "smmu500.SMMU_GATS12UW", 0x128, 1)
        , SMMU_GATS12UW_H("SMMU_GATS12UW_H", "smmu500.SMMU_GATS12UW_H", 0x12C, 1)
        , SMMU_GPAR("SMMU_GPAR", "smmu500.SMMU_GPAR", 0x180, 1)
        , SMMU_GPAR_H("SMMU_GPAR_H", "smmu500.SMMU_GPAR_H", 0x184, 1)
        , SMMU_TBU_PWR_STATUS("SMMU_TBU_PWR_STATUS", "smmu500.SMMU_TBU_PWR_STATUS", 0x2204, 1)
        , SMMU_CB_SCTLR("SMMU_CB_SCTLR", "smmu500.SMMU_CB_SCTLR", 0x000, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_ACTLR("SMMU_CB_ACTLR", "smmu500.SMMU_CB_ACTLR", 0x004, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_RESUME("SMMU_CB_RESUME", "smmu500.SMMU_CB_RESUME", 0x008, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TCR2("SMMU_CB_TCR2", "smmu500.SMMU_CB_TCR2", 0x010, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TTBR0_LOW("SMMU_CB_TTBR0_LOW", "smmu500.SMMU_CB_TTBR0_LOW", 0x020, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TTBR0_HIGH("SMMU_CB_TTBR0_HIGH", "smmu500.SMMU_CB_TTBR0_HIGH", 0x024, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TTBR1_LOW("SMMU_CB_TTBR1_LOW", "smmu500.SMMU_CB_TTBR1_LOW", 0x028, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TTBR1_HIGH("SMMU_CB_TTBR1_HIGH", "smmu500.SMMU_CB_TTBR1_HIGH", 0x02C, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TCR_LPAE("SMMU_CB_TCR_LPAE", "smmu500.SMMU_CB_TCR_LPAE", 0x030, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_CONTEXTIDR("SMMU_CB_CONTEXTIDR", "smmu500.SMMU_CB_CONTEXTIDR", 0x034, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_PRRR_MAIR0("SMMU_CB_PRRR_MAIR0", "smmu500.SMMU_CB_PRRR_MAIR0", 0x038, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_NMRR_MAIR1("SMMU_CB_NMRR_MAIR1", "smmu500.SMMU_CB_NMRR_MAIR1", 0x03C, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_FSR("SMMU_CB_FSR", "smmu500.SMMU_CB_FSR", 0x058, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_FSRRESTORE("SMMU_CB_FSRRESTORE", "smmu500.SMMU_CB_FSRRESTORE", 0x05C, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_FAR_LOW("SMMU_CB_FAR_LOW", "smmu500.SMMU_CB_FAR_LOW", 0x060, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_FAR_HIGH("SMMU_CB_FAR_HIGH", "smmu500.SMMU_CB_FAR_HIGH", 0x064, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_FSYNR0("SMMU_CB_FSYNR0", "smmu500.SMMU_CB_FSYNR0", 0x068, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_FSYNR1("SMMU_CB_FSYNR1", "smmu500.SMMU_CB_FSYNR1", 0x06C, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_IPAFAR_LOW("SMMU_CB_IPAFAR_LOW", "smmu500.SMMU_CB_IPAFAR_LOW", 0x070, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_IPAFAR_HIGH("SMMU_CB_IPAFAR_HIGH", "smmu500.SMMU_CB_IPAFAR_HIGH", 0x074, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TLBIVA_LOW("SMMU_CB_TLBIVA_LOW", "smmu500.SMMU_CB_TLBIVA_LOW", 0x600, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TLBIVA_HIGH("SMMU_CB_TLBIVA_HIGH", "smmu500.SMMU_CB_TLBIVA_HIGH", 0x604, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TLBIVAA_LOW("SMMU_CB_TLBIVAA_LOW", "smmu500.SMMU_CB_TLBIVAA_LOW", 0x608, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TLBIVAA_HIGH("SMMU_CB_TLBIVAA_HIGH", "smmu500.SMMU_CB_TLBIVAA_HIGH", 0x60C, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TLBIASID("SMMU_CB_TLBIASID", "smmu500.SMMU_CB_TLBIASID", 0x610, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TLBIALL("SMMU_CB_TLBIALL", "smmu500.SMMU_CB_TLBIALL", 0x618, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TLBIVAL_LOW("SMMU_CB_TLBIVAL_LOW", "smmu500.SMMU_CB_TLBIVAL_LOW", 0x620, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TLBIVAL_HIGH("SMMU_CB_TLBIVAL_HIGH", "smmu500.SMMU_CB_TLBIVAL_HIGH", 0x624, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TLBIVAAL_LOW("SMMU_CB_TLBIVAAL_LOW", "smmu500.SMMU_CB_TLBIVAAL_LOW", 0x628, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TLBIVAAL_HIGH("SMMU_CB_TLBIVAAL_HIGH", "smmu500.SMMU_CB_TLBIVAAL_HIGH", 0x62C, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TLBIIPAS2_LOW("SMMU_CB_TLBIIPAS2_LOW", "smmu500.SMMU_CB_TLBIIPAS2_LOW", 0x630, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TLBIIPAS2_HIGH("SMMU_CB_TLBIIPAS2_HIGH", "smmu500.SMMU_CB_TLBIIPAS2_HIGH", 0x634, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TLBIIPAS2L_LOW("SMMU_CB_TLBIIPAS2L_LOW", "smmu500.SMMU_CB_TLBIIPAS2L_LOW", 0x638, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TLBIIPAS2L_HIGH("SMMU_CB_TLBIIPAS2L_HIGH", "smmu500.SMMU_CB_TLBIIPAS2L_HIGH", 0x63C, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TLBSYNC("SMMU_CB_TLBSYNC", "smmu500.SMMU_CB_TLBSYNC", 0x7F0, CB_BANK_COUNT, 0x1000ULL)
        , SMMU_CB_TLBSTATUS("SMMU_CB_TLBSTATUS", "smmu500.SMMU_CB_TLBSTATUS", 0x7F4, CB_BANK_COUNT, 0x1000ULL)
        , CB_SCTLR_M(SMMU_CB_SCTLR, "CB_SCTLR.M", 0, 1)
        , CB_SCTLR_CFIE(SMMU_CB_SCTLR, "CB_SCTLR.CFIE", 6, 1)
        , CB_FSR_TF(SMMU_CB_FSR, "CB_FSR.TF", 1, 1)
        , CB_FSR_AFF(SMMU_CB_FSR, "CB_FSR.AFF", 2, 1)
        , CB_FSR_PF(SMMU_CB_FSR, "CB_FSR.PF", 3, 1)
        , CB_FSR_EF(SMMU_CB_FSR, "CB_FSR.EF", 4, 1)
        , CB_FSR_ASF(SMMU_CB_FSR, "CB_FSR.ASF", 7, 1)
        , CB_FSR_MULTI(SMMU_CB_FSR, "CB_FSR.MULTI", 31, 1)
        , CB_FSYNR0_PLVL(SMMU_CB_FSYNR0, "CB_FSYNR0.PLVL", 0, 2)
        , CB_FSYNR0_WNR(SMMU_CB_FSYNR0, "CB_FSYNR0.WNR", 4, 1)
        , CB_FSYNR0_PNU(SMMU_CB_FSYNR0, "CB_FSYNR0.PNU", 5, 1)
        , CB_FSYNR0_IND(SMMU_CB_FSYNR0, "CB_FSYNR0.IND", 6, 1)
        , CB_FSYNR0_NSATTR(SMMU_CB_FSYNR0, "CB_FSYNR0.NSATTR", 8, 1)
        , CB_FSYNR0_ATOF(SMMU_CB_FSYNR0, "CB_FSYNR0.ATOF", 9, 1)
        , CB_FSYNR0_PTWF(SMMU_CB_FSYNR0, "CB_FSYNR0.PTWF", 10, 1)
        , CB_FSYNR0_AFR(SMMU_CB_FSYNR0, "CB_FSYNR0.AFR", 11, 1)
        , CB_FSYNR0_S1CBNDX(SMMU_CB_FSYNR0, "CB_FSYNR0.S1CBNDX", 16, 7)
        , CB_FSYNR1_MID(SMMU_CB_FSYNR1, "CB_FSYNR1.MID", 0, 8)
        , CB_FSYNR1_PID(SMMU_CB_FSYNR1, "CB_FSYNR1.PID", 8, 5)
        , CB_FSYNR1_BID(SMMU_CB_FSYNR1, "CB_FSYNR1.BID", 13, 3)
    {
    }

    void set_cb_bank_base(uint64_t base)
    {
        SMMU_CB_SCTLR.p_offset = base + 0x000;
        SMMU_CB_ACTLR.p_offset = base + 0x004;
        SMMU_CB_RESUME.p_offset = base + 0x008;
        SMMU_CB_TCR2.p_offset = base + 0x010;
        SMMU_CB_TTBR0_LOW.p_offset = base + 0x020;
        SMMU_CB_TTBR0_HIGH.p_offset = base + 0x024;
        SMMU_CB_TTBR1_LOW.p_offset = base + 0x028;
        SMMU_CB_TTBR1_HIGH.p_offset = base + 0x02C;
        SMMU_CB_TCR_LPAE.p_offset = base + 0x030;
        SMMU_CB_CONTEXTIDR.p_offset = base + 0x034;
        SMMU_CB_PRRR_MAIR0.p_offset = base + 0x038;
        SMMU_CB_NMRR_MAIR1.p_offset = base + 0x03C;
        SMMU_CB_FSR.p_offset = base + 0x058;
        SMMU_CB_FSRRESTORE.p_offset = base + 0x05C;
        SMMU_CB_FAR_LOW.p_offset = base + 0x060;
        SMMU_CB_FAR_HIGH.p_offset = base + 0x064;
        SMMU_CB_FSYNR0.p_offset = base + 0x068;
        SMMU_CB_FSYNR1.p_offset = base + 0x06C;
        SMMU_CB_IPAFAR_LOW.p_offset = base + 0x070;
        SMMU_CB_IPAFAR_HIGH.p_offset = base + 0x074;
        SMMU_CB_TLBIVA_LOW.p_offset = base + 0x600;
        SMMU_CB_TLBIVA_HIGH.p_offset = base + 0x604;
        SMMU_CB_TLBIVAA_LOW.p_offset = base + 0x608;
        SMMU_CB_TLBIVAA_HIGH.p_offset = base + 0x60C;
        SMMU_CB_TLBIASID.p_offset = base + 0x610;
        SMMU_CB_TLBIALL.p_offset = base + 0x618;
        SMMU_CB_TLBIVAL_LOW.p_offset = base + 0x620;
        SMMU_CB_TLBIVAL_HIGH.p_offset = base + 0x624;
        SMMU_CB_TLBIVAAL_LOW.p_offset = base + 0x628;
        SMMU_CB_TLBIVAAL_HIGH.p_offset = base + 0x62C;
        SMMU_CB_TLBIIPAS2_LOW.p_offset = base + 0x630;
        SMMU_CB_TLBIIPAS2_HIGH.p_offset = base + 0x634;
        SMMU_CB_TLBIIPAS2L_LOW.p_offset = base + 0x638;
        SMMU_CB_TLBIIPAS2L_HIGH.p_offset = base + 0x63C;
        SMMU_CB_TLBSYNC.p_offset = base + 0x7F0;
        SMMU_CB_TLBSTATUS.p_offset = base + 0x7F4;
    }

    void bind_regs(gs::json_module& jm)
    {
        jm.bind_reg(SMMU_SCR0);
        jm.bind_reg(SMMU_NSCR0);
        jm.bind_reg(SMMU_SCR1);
        jm.bind_reg(SMMU_SACR);
        jm.bind_reg(SMMU_SIDR0);
        jm.bind_reg(SMMU_SIDR1);
        jm.bind_reg(SMMU_SIDR2);
        jm.bind_reg(SMMU_SIDR7);
        jm.bind_reg(SMMU_SGFSR);
        jm.bind_reg(SMMU_NSGFSR);
        jm.bind_reg(SMMU_STLBIALL);
        jm.bind_reg(SMMU_TLBIVMID);
        jm.bind_reg(SMMU_TLBIALLNSNH);
        jm.bind_reg(SMMU_TLBIALLH);
        jm.bind_reg(SMMU_TLBIVAH_LOW);
        jm.bind_reg(SMMU_STLBIVALM_LOW);
        jm.bind_reg(SMMU_STLBIVALM_HIGH);
        jm.bind_reg(SMMU_STLBIVAM_LOW);
        jm.bind_reg(SMMU_STLBIVAM_HIGH);
        jm.bind_reg(SMMU_TLBIVALH64_LOW);
        jm.bind_reg(SMMU_TLBIVALH64_HIGH);
        jm.bind_reg(SMMU_TLBIVMIDS1);
        jm.bind_reg(SMMU_STLBIALLM);
        jm.bind_reg(SMMU_TLBIVAH64_LOW);
        jm.bind_reg(SMMU_TLBIVAH64_HIGH);
        jm.bind_reg(SMMU_SMR);
        jm.bind_reg(SMMU_S2CR);
        jm.bind_reg(SMMU_CBAR);
        jm.bind_reg(SMMU_CBA2R);
        jm.bind_reg(SMMU_GATS1PR);
        jm.bind_reg(SMMU_GATS1PR_H);
        jm.bind_reg(SMMU_GATS1PW);
        jm.bind_reg(SMMU_GATS1PW_H);
        jm.bind_reg(SMMU_GATS1UR);
        jm.bind_reg(SMMU_GATS1UR_H);
        jm.bind_reg(SMMU_GATS1UW);
        jm.bind_reg(SMMU_GATS1UW_H);
        jm.bind_reg(SMMU_GATS12PR);
        jm.bind_reg(SMMU_GATS12PR_H);
        jm.bind_reg(SMMU_GATS12PW);
        jm.bind_reg(SMMU_GATS12PW_H);
        jm.bind_reg(SMMU_GATS12UR);
        jm.bind_reg(SMMU_GATS12UR_H);
        jm.bind_reg(SMMU_GATS12UW);
        jm.bind_reg(SMMU_GATS12UW_H);
        jm.bind_reg(SMMU_GPAR);
        jm.bind_reg(SMMU_GPAR_H);
        jm.bind_reg(SMMU_TBU_PWR_STATUS);
        jm.bind_reg(SMMU_CB_SCTLR);
        jm.bind_reg(SMMU_CB_ACTLR);
        jm.bind_reg(SMMU_CB_RESUME);
        jm.bind_reg(SMMU_CB_TCR2);
        jm.bind_reg(SMMU_CB_TTBR0_LOW);
        jm.bind_reg(SMMU_CB_TTBR0_HIGH);
        jm.bind_reg(SMMU_CB_TTBR1_LOW);
        jm.bind_reg(SMMU_CB_TTBR1_HIGH);
        jm.bind_reg(SMMU_CB_TCR_LPAE);
        jm.bind_reg(SMMU_CB_CONTEXTIDR);
        jm.bind_reg(SMMU_CB_PRRR_MAIR0);
        jm.bind_reg(SMMU_CB_NMRR_MAIR1);
        jm.bind_reg(SMMU_CB_FSR);
        jm.bind_reg(SMMU_CB_FSRRESTORE);
        jm.bind_reg(SMMU_CB_FAR_LOW);
        jm.bind_reg(SMMU_CB_FAR_HIGH);
        jm.bind_reg(SMMU_CB_FSYNR0);
        jm.bind_reg(SMMU_CB_FSYNR1);
        jm.bind_reg(SMMU_CB_IPAFAR_LOW);
        jm.bind_reg(SMMU_CB_IPAFAR_HIGH);
        jm.bind_reg(SMMU_CB_TLBIVA_LOW);
        jm.bind_reg(SMMU_CB_TLBIVA_HIGH);
        jm.bind_reg(SMMU_CB_TLBIVAA_LOW);
        jm.bind_reg(SMMU_CB_TLBIVAA_HIGH);
        jm.bind_reg(SMMU_CB_TLBIASID);
        jm.bind_reg(SMMU_CB_TLBIALL);
        jm.bind_reg(SMMU_CB_TLBIVAL_LOW);
        jm.bind_reg(SMMU_CB_TLBIVAL_HIGH);
        jm.bind_reg(SMMU_CB_TLBIVAAL_LOW);
        jm.bind_reg(SMMU_CB_TLBIVAAL_HIGH);
        jm.bind_reg(SMMU_CB_TLBIIPAS2_LOW);
        jm.bind_reg(SMMU_CB_TLBIIPAS2_HIGH);
        jm.bind_reg(SMMU_CB_TLBIIPAS2L_LOW);
        jm.bind_reg(SMMU_CB_TLBIIPAS2L_HIGH);
        jm.bind_reg(SMMU_CB_TLBSYNC);
        jm.bind_reg(SMMU_CB_TLBSTATUS);
        jm.log_end_of_binding_msg(m_name);
    }
};
} // namespace gs
#endif
