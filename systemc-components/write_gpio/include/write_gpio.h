/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef write_gpio_H
#define write_gpio_H

#include <systemc>
#include <cci_configuration>
#include <cciutils.h>
#include <scp/report.h>
#include <tlm>
#include <cciutils.h>
#include <module_factory_registery.h>
#include <ports/multiinitiator-signal-socket.h>

namespace gs {
class write_gpio : public sc_core::sc_module
{
    SCP_LOGGER();
    cci::cci_broker_handle m_broker;

    cci::cci_param<bool> p_gpio_value;

    void set_gpio_method();

public:
    MultiInitiatorSignalSocket<> gpio_sig_socket;

    SC_HAS_PROCESS(write_gpio);
    write_gpio(sc_core::sc_module_name name);

    void start_of_simulation() {}

    void before_end_of_elaboration() {}
};
} // namespace gs

extern "C" void module_register();

#endif // write_gpio_H
