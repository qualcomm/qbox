/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "write_gpio.h"

namespace gs {

write_gpio::write_gpio(sc_core::sc_module_name _name)
    : sc_core::sc_module(_name)
    , m_broker(cci::cci_get_broker())
    , p_gpio_value("gpio_value", true, "GPIO value to write")
    , gpio_sig_socket("gpio_sig_socket")
{
    SCP_TRACE(())("Constructor");

    SC_METHOD(set_gpio_method);
}

void write_gpio::set_gpio_method()
{
    // Let's set GPIOs/FUSES bind with multi_init_socket to 1
    gpio_sig_socket.async_write_vector({ p_gpio_value });
}
} // namespace gs

typedef gs::write_gpio write_gpio;

void module_register() { GSC_MODULE_REGISTER_C(write_gpio); }