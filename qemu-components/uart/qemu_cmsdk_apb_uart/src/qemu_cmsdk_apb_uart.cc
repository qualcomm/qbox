/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <systemc>

#include <qemu_cmsdk_apb_uart.h>

qemu_cmsdk_apb_uart::qemu_cmsdk_apb_uart(const sc_core::sc_module_name& name, sc_core::sc_object* o)
    : qemu_cmsdk_apb_uart(name, *(dynamic_cast<QemuInstance*>(o)))
{
}

qemu_cmsdk_apb_uart::qemu_cmsdk_apb_uart(const sc_core::sc_module_name& n, QemuInstance& inst)
    : QemuDevice(n, inst, "cmsdk-apb-uart")
    , socket("mem", inst)
    , p_pclk_frq("pclk_frq", 0, "Peripheral clock frequency (Hz)")
    , p_chardev_id("chardev_id", std::string(n), "chardev unique identifier")
    , p_chardev_backend("chardev_backend", "stdio", "chardev device backend")
    , txint("txint")
    , rxint("rxint")
    , txovrint("txovrint")
    , rxovrint("rxovrint")
    , uartint("uartint")
{
}

void qemu_cmsdk_apb_uart::before_end_of_elaboration()
{
    QemuDevice::before_end_of_elaboration();

    if (p_pclk_frq.get_value() == 0) {
        SC_REPORT_FATAL(name(), "pclk_frq must have a non-zero value");
    }

    m_chardev = m_inst.get().chardev_new(p_chardev_id.get_value(), p_chardev_backend.get_value());

    m_dev.set_prop_chardev("chardev", m_chardev);
    m_dev.set_prop_int("pclk-frq", p_pclk_frq);
}

void qemu_cmsdk_apb_uart::end_of_elaboration()
{
    QemuDevice::set_sysbus_as_parent_bus();
    QemuDevice::end_of_elaboration();

    qemu::SysBusDevice sbd(m_dev);

    socket.init(sbd, 0);

    txint.init_sbd(sbd, 0);
    rxint.init_sbd(sbd, 1);
    txovrint.init_sbd(sbd, 2);
    rxovrint.init_sbd(sbd, 3);
    uartint.init_sbd(sbd, 4);
}

void module_register() { GSC_MODULE_REGISTER_C(qemu_cmsdk_apb_uart, sc_core::sc_object*); }
