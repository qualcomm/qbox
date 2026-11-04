/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef aoss_pmic_arb_pmic_arb_core_H
#define aoss_pmic_arb_pmic_arb_core_H

#include <systemc>
#include <cci_configuration>
#include <cciutils.h>
#include <scp/report.h>
#include <tlm>
#include <cciutils.h>
#include <module_factory_registery.h>
#include "aoss_pmic_arb_pmic_arb_core_gen.h"
#include <ports/multiinitiator-signal-socket.h>
/**
 * The model was generated using this command:
 * python3 ./scripts/template_module/get_ipcat_info.py --chip lemansau_2.0 --memory-map-id 1646 \
 *  --hierarchy-name lemansau.aoss.pmic_arb.pmic_arb_core_registers \
 *  --module-name aoss_pmic_arb_pmic_arb_core --output-path ./tmp \
 *  --init-template-dir ./scripts/template_module  --generate-model
 */

namespace quic {
class aoss_pmic_arb_pmic_arb_core : public sc_core::sc_module, public aoss_pmic_arb_pmic_arb_core_gen
{
    SCP_LOGGER();
    cci::cci_broker_handle m_broker; // Should be a private broker for efficiency, but requires fixes to cci utils to
                                     // prevent use of cci_get_broker()
    gs::json_zip_archive m_jza;
    bool loaded_ok;
    gs::json_module M;

    sc_core::sc_event reset_ev;

    sc_core::sc_time last_reset = sc_core::SC_ZERO_TIME;
    void do_reset()
    {
        // Insist that SystemC time moves forward at least by more than a zero timed delta between resets
        // (Call it a debounce protection!)
        if (last_reset == sc_core::sc_time_stamp()) return;

        SCP_INFO(())("Initiate Reset sequence");

        // Actually send the reset pulse. In this case we are driving the whole reset activity
        // we'll send a positive then negative edge - from an sc_thread. see the MultiInitiatorSignalSocket
        // convenience socket.
        reset.async_write_vector({ 1, 0 });
        last_reset = sc_core::sc_time_stamp();
    }

public:
    tlm_utils::multi_passthrough_target_socket<aoss_pmic_arb_pmic_arb_core> target_socket;
    TargetSignalSocket<bool> reset_in;

    MultiInitiatorSignalSocket<> reset;

    aoss_pmic_arb_pmic_arb_core(sc_core::sc_module_name name, sc_core::sc_object* _router,
                                sc_core::sc_object* _reg_memory);

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
        /* Add callbacks tt the registers*/
        /* e.g. MYREG.post_write([&](TXN(txn)) {... my function...}); */
        PMIC_ARB_CHNL_CMD.post_write([&](TXN()) {
            SCP_DEBUG(())
            ("PMIC_ARB_CHNL_CMD was written to with value: {:x} (the OPCODE is {}) ", PMIC_ARB_CHNL_CMD[0],
             (int)OPCODE);
            if (OPCODE == 0x3) reset_ev.notify();
        });
    }
};
} // namespace quic

extern "C" void module_register();

#endif // aoss_pmic_arb_pmic_arb_core_H
