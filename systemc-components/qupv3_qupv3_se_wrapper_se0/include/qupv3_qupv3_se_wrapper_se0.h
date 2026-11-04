/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef qupv3_qupv3_se_wrapper_se0_H
#define qupv3_qupv3_se_wrapper_se0_H

#include <systemc>
#include <cci_configuration>
#include <cciutils.h>
#include <scp/report.h>
#include <tlm>
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <async_event.h>
#include <ports/biflow-socket.h>
#include <module_factory_registery.h>
#include <tlm_sockets_buswidth.h>
#include "qupv3_qupv3_se_wrapper_se0_gen.h"

/**
 * The model was generated using this command:
 * python3 ./scripts/template_module/get_ipcat_info.py --chip lemansau_2.0 --memory-map-id 1646    \
 * --hierarchy-name lemansau.qupv3_2_qupv3_id_7.qupv3_2_qupv3_se_wrapper.qupv3_2_se0_se0  --module-name
 * qupv3.qupv3_se_wrapper.se0  \
 * --output-path ./ --init-template-dir ./scripts/template_module  --generate-model
 */

namespace quic {

class qupv3_qupv3_se_wrapper_se0 : public sc_core::sc_module, public qupv3_qupv3_se_wrapper_se0_gen
{
public:
    SCP_LOGGER();
    cci::cci_broker_handle m_broker; // Should be a private broker for efficiency
    gs::json_zip_archive m_jza;
    bool loaded_ok;
    gs::json_module M;
    sc_core::sc_event irq_update;

    tlm_utils::multi_passthrough_target_socket<qupv3_qupv3_se_wrapper_se0, DEFAULT_TLM_BUSWIDTH> target_socket;
    TargetSignalSocket<bool> reset;
    gs::biflow_socket<qupv3_qupv3_se_wrapper_se0> backend_socket;
    InitiatorSignalSocket<bool> irq;
    tlm_utils::simple_initiator_socket<qupv3_qupv3_se_wrapper_se0, DEFAULT_TLM_BUSWIDTH> dma_socket;
    qupv3_qupv3_se_wrapper_se0(sc_core::sc_module_name _name);
    virtual void do_reset(bool value);

protected:
    tlm::tlm_dmi dmi_data;
    bool m_dmi_valid;
    bool dma_read(uint64_t addr, uint8_t* data, uint32_t size);
    bool dma_write(uint64_t addr, uint8_t* data, uint32_t size);

    virtual void update_irq() = 0;
    virtual void receive(tlm::tlm_generic_payload& txn, sc_core::sc_time& t) = 0;
    virtual void end_of_elaboration() = 0;
    virtual void start_of_simulation() = 0;
};
} // namespace quic
/* clang-format on */
extern "C" void module_register();
#endif // qupv3_qupv3_se_wrapper_se0_H
