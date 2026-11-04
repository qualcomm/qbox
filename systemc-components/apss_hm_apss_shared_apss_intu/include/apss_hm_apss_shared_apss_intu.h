/*
 * Quic Module apss_hm_apss_shared_apss_intu
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef apss_hm_apss_shared_apss_intu_H
#define apss_hm_apss_shared_apss_intu_H

#include <systemc>
#include <cci_configuration>
#include <cciutils.h>
#include <scp/report.h>
#include <tlm>
#include <ports/biflow-socket.h>
#include <cciutils.h>
#include <module_factory_registery.h>
#include "apss_hm_apss_shared_apss_intu_gen.h"
#include "sysc/kernel/sc_time.h"
#include "tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h"
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>

namespace quic {
class apss_hm_apss_shared_apss_intu : public sc_core::sc_module, public apss_hm_apss_shared_apss_intu_gen
{
    SCP_LOGGER();
    cci::cci_broker_handle m_broker; // Should be a private broker for efficiency, but requires fixes to cci utils to
                                     // prevent use of cci_get_broker()
    gs::json_zip_archive m_jza;
    bool loaded_ok;
    gs::json_module M;

public:
    tlm_utils::multi_passthrough_target_socket<apss_hm_apss_shared_apss_intu> target_socket;
    TargetSignalSocket<bool> reset;
    /*
     * Below "cdsp_ipc" needs to align with the lua file's expectation:
     *  cdsp_ipc_# = {bind = "&hex_plugin.target_signal_socket_0"},
     */
#define NUM_INTS 3
    sc_core::sc_vector<InitiatorSignalSocket<bool>> cdsp_ipc;

    apss_hm_apss_shared_apss_intu(sc_core::sc_module_name name);

    void start_of_simulation()
    {
        /* NB writes should not happen before simulation starts,
         * Write any special register initial values here (e.g. over-right what is in the
         * configuration files)
         */
    }
    void before_end_of_elaboration()
    {
        SCP_TRACE(())("Before End of Elaboration, registering callbacks");

        IPC_INTERRUPT.post_write([&](TXN()) {
            SCP_DEBUG(())
            ("IPC_INTERRUPT was written to with value: {:x}) ", IPC_INTERRUPT[0]);
            uint32_t irq = CDSP_IPC;
            if (irq & 0x1) {
                cdsp_ipc[0]->write(1);
            }
            if (irq & 0x2) {
                cdsp_ipc[1]->write(1);
            }
            if (irq & 0x4) {
                cdsp_ipc[2]->write(1);
            }
        });
    }
};
} // namespace quic

extern "C" void module_register();

#endif // apss_hm_apss_shared_apss_intu_H
