/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef aoss_pmic_arb_pmic_arb_core_gen_H
#define aoss_pmic_arb_pmic_arb_core_gen_H

/**
 * This header file is auto generated using scripts/template_module/get_ipcat_info.py.
 * please uncomment the registers that you need to use only and make sure to resolve
 * name collisions by renaming the registers and fields to the name you prefer in case
 * 2 or more registers or fields have the same name.
 *
 * Register of interest should be uncommnented in 3 places in the file:
 * 1- Declaration.
 * 2- Constructor Initialization.
 * 3- Binding.
 *
 * Field of interest should be uncommented in 2 places:
 * 1- Declaration.
 * 2- Constructor Initialization.
 */

#include <reg_model_maker/reg_model_maker.h>

namespace quic {
class aoss_pmic_arb_pmic_arb_core_gen
{
private:
    std::string m_name;

public:
    gs::gs_register<uint32_t> PMIC_ARB_CHNL_CMD;
    gs::gs_field<uint32_t> OPCODE;

    aoss_pmic_arb_pmic_arb_core_gen()
        : m_name("aoss_pmic_arb_pmic_arb_core_gen")
        , PMIC_ARB_CHNL_CMD("PMIC_ARB_CHNL_CMD", "pmic_arb_core_registers.PMIC_ARB_CHNL_CMD", 0x0, 8372225)
        , OPCODE(PMIC_ARB_CHNL_CMD, PMIC_ARB_CHNL_CMD.get_regname() + ".OPCODE", 27, 5)
    {
    }

    void bind_regs(gs::json_module& jm)
    {
        jm.bind_reg(PMIC_ARB_CHNL_CMD);

        jm.log_end_of_binding_msg(m_name);
    }
};
} // namespace quic
#endif