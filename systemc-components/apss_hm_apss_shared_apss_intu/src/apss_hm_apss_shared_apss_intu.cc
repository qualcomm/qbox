/*
 * Quic Module apss_hm_apss_shared_apss_intu
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#define gen_xstr(s) _gen_str(s)
#define _gen_str(s) #s

#define INCBIN_SILENCE_BITCODE_WARNING
#include <reg_model_maker/incbin.h>

INCBIN(ZipArchive_apss_hm_apss_shared_apss_intu_, __FILE__ "_config.zip");

#include "apss_hm_apss_shared_apss_intu.h"
#include <zip.h>

namespace quic {

apss_hm_apss_shared_apss_intu::apss_hm_apss_shared_apss_intu(sc_core::sc_module_name _name)
    : sc_core::sc_module(_name)
    , m_broker(cci::cci_get_broker())
    , m_jza(zip_open_from_source(zip_source_buffer_create(gZipArchive_apss_hm_apss_shared_apss_intu_Data,
                                                          gZipArchive_apss_hm_apss_shared_apss_intu_Size, 0, nullptr),
                                 ZIP_RDONLY, nullptr))
    , loaded_ok(m_jza.json_read_cci(m_broker.create_broker_handle(), std::string(name()) + ".apss_shared_apss_intu"))
    , M("apss_shared_apss_intu", m_jza)
    , target_socket("target_socket")
    , reset("reset")
    , cdsp_ipc("cdsp_ipc", NUM_INTS)
{
    printf("%s\n", __func__);
    SCP_TRACE(())("Constructor");
    sc_assert(loaded_ok);
    bind_regs(M);
    target_socket.bind(M.target_socket);
    reset.register_value_changed_cb([&](bool value) {
        if (value) {
            SCP_WARN(()) << "Reset";
        }
        M.reset(value);
    });
}
} // namespace quic

typedef quic::apss_hm_apss_shared_apss_intu apss_hm_apss_shared_apss_intu;

void module_register() { GSC_MODULE_REGISTER_C(apss_hm_apss_shared_apss_intu); }
