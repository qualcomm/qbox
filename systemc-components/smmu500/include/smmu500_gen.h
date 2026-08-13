/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef smmu500_gen_H
#define smmu500_gen_H

#include <reg_model_maker/reg_model_maker.h>
#include <vector>
#include <memory>
#include <string>

namespace gs {
class smmu500_gen
{
private:
    std::string m_name;

public:
    gs::gs_register<uint32_t> SMMU_SCR0;
    gs::gs_field<uint32_t> SCR0_CLIENTPD;
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
    gs::gs_register<uint32_t> SMMU_SMR;
    gs::gs_field<uint32_t> SMR_ID;
    gs::gs_field<uint32_t> SMR_MASK;
    gs::gs_field<uint32_t> SMR_VALID;
    gs::gs_register<uint32_t> SMMU_S2CR;
    gs::gs_field<uint32_t> S2CR_CBNDX_VMID;
    gs::gs_field<uint32_t> S2CR_TYPE;
    gs::gs_register<uint32_t> SMMU_CBAR;
    gs::gs_field<uint32_t> CBAR_VMID;
    gs::gs_field<uint32_t> CBAR_TYPE;
    gs::gs_register<uint32_t> SMMU_CBA2R;
    gs::gs_field<uint32_t> CBA2R_VA64;
    gs::gs_register<uint32_t> SMMU_GATS1PR;
    gs::gs_register<uint32_t> SMMU_GATS1PR_H;
    gs::gs_register<uint32_t> SMMU_GATS1PW;
    gs::gs_register<uint32_t> SMMU_GATS1PW_H;
    gs::gs_register<uint32_t> SMMU_GATS12PR;
    gs::gs_register<uint32_t> SMMU_GATS12PR_H;
    gs::gs_register<uint32_t> SMMU_GATS12PW;
    gs::gs_register<uint32_t> SMMU_GATS12PW_H;
    gs::gs_register<uint32_t> SMMU_GPAR;
    gs::gs_register<uint32_t> SMMU_GPAR_H;
    gs::gs_register<uint32_t> SMMU_TBU_PWR_STATUS;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_SCTLR;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_ACTLR;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_RESUME;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_TCR2;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_TTBR0_LOW;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_TTBR0_HIGH;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_TTBR1_LOW;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_TTBR1_HIGH;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_TCR_LPAE;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_CONTEXTIDR;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_PRRR_MAIR0;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_NMRR_MAIR1;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_FSR;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_FSRRESTORE;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_FAR_LOW;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_FAR_HIGH;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_FSYNR0;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_IPAFAR_LOW;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_IPAFAR_HIGH;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_TLBIASID;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_TLBIALL;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_TLBSYNC;
    std::vector<std::shared_ptr<gs::gs_register<uint32_t>>> SMMU_CB_TLBSTATUS;

    /* CB register fields (parent reg is a placeholder for bit_start/bit_length only) */
    gs::gs_field<uint32_t> CB_SCTLR_M;
    gs::gs_field<uint32_t> CB_SCTLR_CFIE;
    gs::gs_field<uint32_t> CB_FSR_TF;

    smmu500_gen()
        : m_name("smmu500_gen")
        , SMMU_SCR0("SMMU_SCR0", "smmu500.SMMU_SCR0", 0x0, 1)
        , SCR0_CLIENTPD(SMMU_SCR0, SMMU_SCR0.get_regname() + ".CLIENTPD", 0, 1)
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
        , SMMU_SMR("SMMU_SMR", "smmu500.SMMU_SMR", 0x800, 224)
        , SMR_ID(SMMU_SMR, SMMU_SMR.get_regname() + ".ID", 0, 15)
        , SMR_MASK(SMMU_SMR, SMMU_SMR.get_regname() + ".MASK", 16, 15)
        , SMR_VALID(SMMU_SMR, SMMU_SMR.get_regname() + ".VALID", 31, 1)
        , SMMU_S2CR("SMMU_S2CR", "smmu500.SMMU_S2CR", 0xc00, 224)
        , S2CR_CBNDX_VMID(SMMU_S2CR, SMMU_S2CR.get_regname() + ".CBNDX_VMID", 0, 8)
        , S2CR_TYPE(SMMU_S2CR, SMMU_S2CR.get_regname() + ".TYPE", 16, 2)
        , SMMU_CBAR("SMMU_CBAR", "smmu500.SMMU_CBAR", 0x1000, 128)
        , CBAR_VMID(SMMU_CBAR, SMMU_CBAR.get_regname() + ".VMID", 0, 8)
        , CBAR_TYPE(SMMU_CBAR, SMMU_CBAR.get_regname() + ".TYPE", 16, 2)
        , SMMU_CBA2R("SMMU_CBA2R", "smmu500.SMMU_CBA2R", 0x1800, 128)
        , CBA2R_VA64(SMMU_CBA2R, SMMU_CBA2R.get_regname() + ".VA64", 0, 1)
        , SMMU_GATS1PR("SMMU_GATS1PR", "smmu500.SMMU_GATS1PR", 0x110, 1)
        , SMMU_GATS1PR_H("SMMU_GATS1PR_H", "smmu500.SMMU_GATS1PR_H", 0x114, 1)
        , SMMU_GATS1PW("SMMU_GATS1PW", "smmu500.SMMU_GATS1PW", 0x118, 1)
        , SMMU_GATS1PW_H("SMMU_GATS1PW_H", "smmu500.SMMU_GATS1PW_H", 0x11C, 1)
        , SMMU_GATS12PR("SMMU_GATS12PR", "smmu500.SMMU_GATS12PR", 0x130, 1)
        , SMMU_GATS12PR_H("SMMU_GATS12PR_H", "smmu500.SMMU_GATS12PR_H", 0x134, 1)
        , SMMU_GATS12PW("SMMU_GATS12PW", "smmu500.SMMU_GATS12PW", 0x138, 1)
        , SMMU_GATS12PW_H("SMMU_GATS12PW_H", "smmu500.SMMU_GATS12PW_H", 0x13C, 1)
        , SMMU_GPAR("SMMU_GPAR", "smmu500.SMMU_GPAR", 0x180, 1)
        , SMMU_GPAR_H("SMMU_GPAR_H", "smmu500.SMMU_GPAR_H", 0x184, 1)
        , SMMU_TBU_PWR_STATUS("SMMU_TBU_PWR_STATUS", "smmu500.SMMU_TBU_PWR_STATUS", 0x2204, 1)
        , CB_SCTLR_M(SMMU_SCR0, "CB_SCTLR.M", 0, 1)
        , CB_SCTLR_CFIE(SMMU_SCR0, "CB_SCTLR.CFIE", 6, 1)
        , CB_FSR_TF(SMMU_SGFSR, "CB_FSR.TF", 1, 1)
    {
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
        jm.bind_reg(SMMU_SMR);
        jm.bind_reg(SMMU_S2CR);
        jm.bind_reg(SMMU_CBAR);
        jm.bind_reg(SMMU_CBA2R);
        jm.bind_reg(SMMU_GATS1PR);
        jm.bind_reg(SMMU_GATS1PR_H);
        jm.bind_reg(SMMU_GATS1PW);
        jm.bind_reg(SMMU_GATS1PW_H);
        jm.bind_reg(SMMU_GATS12PR);
        jm.bind_reg(SMMU_GATS12PR_H);
        jm.bind_reg(SMMU_GATS12PW);
        jm.bind_reg(SMMU_GATS12PW_H);
        jm.bind_reg(SMMU_GPAR);
        jm.bind_reg(SMMU_GPAR_H);
        jm.bind_reg(SMMU_TBU_PWR_STATUS);
        jm.log_end_of_binding_msg(m_name);
    }
};
} // namespace gs
#endif
