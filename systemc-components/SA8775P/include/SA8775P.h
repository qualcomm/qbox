/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SA8775P_H
#define SA8775P_H

#include <systemc>
#include <cci_configuration>
#include <cciutils.h>
#include <scp/report.h>
#include <tlm>
#include <cciutils.h>
#include <module_factory_registery.h>
#include <ports/target-signal-socket.h>
#include <reg_model_maker/reg_model_maker.h>
#include <SA8775P_gen.h>
namespace quic {
class SA8775P : public sc_core::sc_module, public SA8775P_gen
{
    SCP_LOGGER();

protected:
    cci::cci_param<bool> is_shared_memories;
    cci::cci_param<std::string> p_zipfile;

public:
    SA8775P(sc_core::sc_module_name _name, sc_core::sc_object* router, sc_core::sc_object* reg_memory);
    cci::cci_broker_handle m_broker;
    zip_t* m_archive;
    gs::router<>* m_router;
    gs::gs_memory<>* m_reg_memory;
    std::unique_ptr<gs::reg_router<>> m_reg_router;
    sc_core::sc_vector<gs::gs_memory<>> memories;
    sc_core::sc_port<sc_core::sc_signal_inout_if<bool>, 0, sc_core::SC_ZERO_OR_MORE_BOUND> reset_mem;
    TargetSignalSocket<bool> reset_in;

    void do_reset(bool value);
};
} // namespace quic

extern "C" void module_register();

#endif // SA8775P_H
