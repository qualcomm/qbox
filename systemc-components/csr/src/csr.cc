/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define gen_xstr(s) _gen_str(s)
#define _gen_str(s) #s

#define INCBIN_SILENCE_BITCODE_WARNING
#include <reg_model_maker/incbin.h>

INCBIN(ZipArchive_csr_, __FILE__ "_config.zip");

#include "csr.h"
#include <zip.h>

namespace quic {

csr::csr(sc_core::sc_module_name _name)
    : sc_core::sc_module(_name)
    , m_broker(cci::cci_get_broker())
    , m_jza(zip_open_from_source(zip_source_buffer_create(gZipArchive_csr_Data, gZipArchive_csr_Size, 0, nullptr),
                                 ZIP_RDONLY, nullptr))
    , loaded_ok(m_jza.json_read_cci(m_broker, std::string(name()) + ".qdsp6v69ss_csr"))
    , M("qdsp6v69ss_csr", m_jza)
    , p_instance_id("instance_id", 0, "QDSP6SS_INSTANCE_ID")
    , target_socket("target_socket")
    , reset("reset")
{
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

typedef quic::csr csr;

void module_register() { GSC_MODULE_REGISTER_C(csr); }
