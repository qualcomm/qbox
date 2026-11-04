/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef csr_gen_H
#define csr_gen_H

/**
 * This header file is auto generated using
 * scripts/template_module/get_ipcat_info.py. please uncomment the registers
 * that you need to use only and make sure to resolve name collisions by
 * renaming the registers and fields to the name you prefer in case 2 or more
 * registers or fields have the same name.
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
class csr_gen
{
private:
    std::string m_name;

public:
    gs::gs_register<uint32_t> QDSP6SS_INSTANCE_ID;
    gs::gs_field<uint32_t> ID;

    csr_gen()
        : m_name("csr_gen")
        , QDSP6SS_INSTANCE_ID("QDSP6SS_INSTANCE_ID", "qdsp6v69ss_csr.QDSP6SS_INSTANCE_ID", 0x0, 1)
        , ID(QDSP6SS_INSTANCE_ID, QDSP6SS_INSTANCE_ID.get_regname() + ".ID", 0, 3)
    {
    }

    void bind_regs(gs::json_module& jm)
    {
        jm.bind_reg(QDSP6SS_INSTANCE_ID);

        jm.log_end_of_binding_msg(m_name);
    }
};
} // namespace quic
#endif