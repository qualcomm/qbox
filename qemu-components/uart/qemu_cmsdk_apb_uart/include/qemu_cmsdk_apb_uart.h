/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _LIBQBOX_COMPONENTS_UART_PL011_H
#define _LIBQBOX_COMPONENTS_UART_PL011_H

#include <cci_configuration>

#include <libgssync.h>
#include <module_factory_registery.h>

#include <qemu-instance.h>

#include <device.h>
#include <ports/target.h>
#include <ports/qemu-initiator-signal-socket.h>

class qemu_cmsdk_apb_uart : public QemuDevice
{
protected:
    qemu::Chardev m_chardev;

public:
    QemuTargetSocket<> socket;

    cci::cci_param<uint32_t> p_pclk_frq;
    cci::cci_param<std::string> p_chardev_id;
    cci::cci_param<std::string> p_chardev_backend;

    QemuInitiatorSignalSocket txint;
    QemuInitiatorSignalSocket rxint;
    QemuInitiatorSignalSocket txovrint;
    QemuInitiatorSignalSocket rxovrint;
    QemuInitiatorSignalSocket uartint;

    qemu_cmsdk_apb_uart(const sc_core::sc_module_name& name, sc_core::sc_object* o);
    qemu_cmsdk_apb_uart(const sc_core::sc_module_name& n, QemuInstance& inst);

    void before_end_of_elaboration() override;
    void end_of_elaboration() override;
};

extern "C" void module_register();

#endif
