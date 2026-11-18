/*
 * This file is part of libqbox
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_USB_KBD_H
#define _LIBQBOX_COMPONENTS_USB_KBD_H

#include <module_factory_registery.h>

#include <qemu_xhci.h>

class qemu_kbd : public qemu_xhci::Device
{
    cci::cci_param<std::string> p_display;

public:
    qemu_kbd(const sc_core::sc_module_name& name, sc_core::sc_object* o, sc_core::sc_object* t)
        : qemu_kbd(name, *(dynamic_cast<QemuInstance*>(o)), (dynamic_cast<qemu_xhci*>(t)))
    {
    }
    qemu_kbd(const sc_core::sc_module_name& n, QemuInstance& inst, qemu_xhci* xhci)
        : qemu_xhci::Device(n, inst, "usb-kbd")
        , p_display("display", "", "QEMU display device receiving keyboard input")
    {
        set_dev_props = [this]() {
            if (!p_display.get_value().empty()) {
                m_dev.set_prop_str("display", p_display.get_value().c_str());
            }
        };
        xhci->add_device(*this);
    }
};

extern "C" void module_register();
#endif
