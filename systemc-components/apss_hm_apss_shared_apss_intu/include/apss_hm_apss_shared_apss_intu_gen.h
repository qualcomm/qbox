/*
 * Quic Module apss_hm_apss_shared_apss_intu
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef apss_hm_apss_shared_apss_intu_gen_H
#define apss_hm_apss_shared_apss_intu_gen_H

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
class apss_hm_apss_shared_apss_intu_gen
{
private:
    std::string m_name;

public:
    gs::gs_register<uint32_t> IPC_INTERRUPT;
    gs::gs_field<uint32_t> CDSP_IPC;

    apss_hm_apss_shared_apss_intu_gen()
        : m_name("apss_hm_apss_shared_apss_intu_gen")
        , IPC_INTERRUPT("IPC_INTERRUPT", "apss_shared_apss_intu.IPC_INTERRUPT", 0xc, 1)
        , CDSP_IPC(IPC_INTERRUPT, IPC_INTERRUPT.get_regname() + ".CDSP_IPC", 4, 4)
    {
    }

    void bind_regs(gs::json_module& jm)
    {
        jm.bind_reg(IPC_INTERRUPT);

        jm.log_end_of_binding_msg(m_name);
    }
};
} // namespace quic
#endif
