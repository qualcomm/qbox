/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#define gen_xstr(s) _gen_str(s)
#define _gen_str(s) #s

#define INCBIN_SILENCE_BITCODE_WARNING
#include <reg_model_maker/incbin.h>

INCBIN(ZipArchive_aoss_pmic_arb_pmic_arb_core_, __FILE__ "_config.zip");

#include "aoss_pmic_arb_pmic_arb_core.h"
#include <zip.h>

namespace quic {

aoss_pmic_arb_pmic_arb_core::aoss_pmic_arb_pmic_arb_core(sc_core::sc_module_name _name, sc_core::sc_object* _router,
                                                         sc_core::sc_object* _reg_memory)
    : sc_core::sc_module(_name)
    , m_broker(cci::cci_get_broker())
    , m_jza(zip_open_from_source(zip_source_buffer_create(gZipArchive_aoss_pmic_arb_pmic_arb_core_Data,
                                                          gZipArchive_aoss_pmic_arb_pmic_arb_core_Size, 0, nullptr),
                                 ZIP_RDONLY, nullptr))
    , loaded_ok(m_jza.json_read_cci(m_broker, std::string(name()) + ".pmic_arb_core_registers", true))
    , M("pmic_arb_core_registers", m_jza, dynamic_cast<gs::router<>*>(_router),
        dynamic_cast<gs::gs_memory<>*>(_reg_memory))
    , target_socket("target_socket")
    , reset_in("reset_in")
    , reset("reset")
{
    SCP_TRACE(())("Constructor");
    sc_assert(loaded_ok);
    bind_regs(M);

    target_socket.bind(M.target_socket);

    SC_METHOD(do_reset);
    sensitive << reset_ev;
    dont_initialize();

    reset_in.register_value_changed_cb([&](bool value) {
        if (value) reset_ev.notify();
    });
}
} // namespace quic

typedef quic::aoss_pmic_arb_pmic_arb_core aoss_pmic_arb_pmic_arb_core;

void module_register() { GSC_MODULE_REGISTER_C(aoss_pmic_arb_pmic_arb_core, sc_core::sc_object*, sc_core::sc_object*); }
