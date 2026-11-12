/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef rsc_H
#define rsc_H

#include <systemc>
#include <cci_configuration>
#include <cciutils.h>
#include <scp/report.h>
#include <tlm>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <module_factory_registery.h>
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include "rsc_gen.h"

namespace quic {

class rsc : public sc_core::sc_module, public rsc_gen
{
    SCP_LOGGER();
    cci::cci_broker_handle m_broker;
    gs::json_zip_archive m_jza;
    bool loaded_ok;
    gs::json_module M;
    sc_core::sc_event set_irq_ev;
    sc_core::sc_event clear_irq_ev;

    static constexpr uint32_t NUM_DRV = 4;
    static constexpr uint32_t CMDS_PER_TCS = 16;
    static constexpr uint32_t AMC_MODE_EN = (1u << 16);
    static constexpr uint32_t AMC_MODE_TRIGGER = (1u << 24);
    static constexpr uint32_t TCS_STATUS_IDLE = (1u << 0);
    static constexpr uint32_t CMD_STATUS_DONE = (1u << 0) | (1u << 8) | (1u << 16);

    // Per-DRV views of the generated registers (TCS counts differ per DRV: 4, 4, 8, 3).
    struct drv_view {
        reg_bank* control;
        reg_bank* status;
        reg_bank* cmd_enable;
        reg_bank* cmd_status;
        reg_bank* cmd_read_resp;
        gs::gs_register<uint32_t>* irq_enable;
        gs::gs_register<uint32_t>* irq_status;
        gs::gs_register<uint32_t>* irq_clear;
        uint32_t num_tcs;
    };
    drv_view m_drv[NUM_DRV];

public:
    tlm_utils::multi_passthrough_target_socket<rsc> target_socket;
    TargetSignalSocket<bool> reset;
    // AMC completion interrupt per DRV (GIC SPI 3/4/5 for DRV0/1/2 on SA8775P).
    sc_core::sc_vector<InitiatorSignalSocket<bool>> irq;

    rsc(sc_core::sc_module_name name);

    void start_of_simulation() {}
    void before_end_of_elaboration();

private:
    void trigger_tcs(uint32_t drv, uint32_t tcs);
    void set_irq();
    void clear_irq();
    void update_irq(uint32_t drv);
};
} // namespace quic

extern "C" void module_register();

#endif // rsc_H
