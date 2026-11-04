/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef csr_H
#define csr_H

#include "csr_gen.h"
#include <cci_configuration>
#include <cciutils.h>
#include <module_factory_registery.h>
#include <scp/report.h>
#include <systemc>
#include <tlm>

namespace quic {
class csr : public sc_core::sc_module, public csr_gen
{
    SCP_LOGGER();
    cci::cci_broker_handle m_broker; // Should be a private broker for efficiency, but requires fixes
                                     // to cci utils to prevent use of cci_get_broker()
    gs::json_zip_archive m_jza;
    bool loaded_ok;
    gs::json_module M;

public:
    cci::cci_param<uint32_t> p_instance_id;
    tlm_utils::multi_passthrough_target_socket<csr> target_socket;
    TargetSignalSocket<bool> reset;

    csr(sc_core::sc_module_name name);

    void start_of_simulation()
    {
        /* NB writes should not happen before simulation starts,
         * Write any special register initial values here (e.g. over-right what is
         * in the configuration files)
         */
    }
    void before_end_of_elaboration()
    {
        SCP_TRACE(())("Before End of Elaboration, registering callbacks");
        QDSP6SS_INSTANCE_ID.pre_read([&](TXN()) {
            /*
             * p_instance_id is the complete architectural value.  In v81,
             * the NSP number is encoded in bits [15:8], while ID describes
             * only the low field.  Write the whole register here instead of
             * assigning through ID, since field assignment must mask bits
             * outside the field.
             */
            QDSP6SS_INSTANCE_ID = p_instance_id.get_value();
            QDSP6SS_INSTANCE_ID.set_mask(0x0); // READ ONLY
            SCP_DEBUG(()) << "QDSP6SS_INSTANCE_ID = " << p_instance_id.get_value();
        });
    }
};
} // namespace quic

extern "C" void module_register();

#endif // csr_H
