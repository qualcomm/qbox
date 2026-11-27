/*
 * This file is part of libqbox
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_USB_TABLET_H
#define _LIBQBOX_COMPONENTS_USB_TABLET_H

#include <module_factory_registery.h>

#include <qemu_xhci.h>

/// @brief Tablet touch device (mouse)
class qemu_tablet : public qemu_xhci::Device
{
    cci::cci_param<std::string> p_display;
    cci::cci_param<uint32_t> p_head;

public:
    qemu_tablet(const sc_core::sc_module_name& name, sc_core::sc_object* o, sc_core::sc_object* t)
        : qemu_tablet(name, *(dynamic_cast<QemuInstance*>(o)), (dynamic_cast<qemu_xhci*>(t)))
    {
    }
    qemu_tablet(const sc_core::sc_module_name& n, QemuInstance& inst, qemu_xhci* xhci)
        : qemu_xhci::Device(n, inst, "usb-tablet")
        , p_display("display", "", "QEMU display device receiving tablet input")
        , p_head("head", 0, "QEMU display head")
    {
        set_dev_props = [this]() {
            if (!p_display.get_value().empty()) {
                m_dev.set_prop_str("display", p_display.get_value().c_str());
            }
            m_dev.set_prop_uint("head", p_head.get_value());
        };
        xhci->add_device(*this);
    }
};

extern "C" void module_register();
#endif
