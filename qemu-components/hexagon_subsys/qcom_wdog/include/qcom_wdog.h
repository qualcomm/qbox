/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_HEXAGON_SUBSYS_QCOM_WDOG_H
#define _LIBQBOX_COMPONENTS_HEXAGON_SUBSYS_QCOM_WDOG_H

#include <module_factory_registery.h>
#include <device.h>
#include <ports/target.h>

class qcom_wdog : public QemuDevice
{
public:
    QemuTargetSocket<> socket;

    qcom_wdog(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : qcom_wdog(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }

    qcom_wdog(const sc_core::sc_module_name& name, QemuInstance& inst)
        : QemuDevice(name, inst, "wdog"), socket("mem", inst)
    {
    }

    void before_end_of_elaboration() override { QemuDevice::before_end_of_elaboration(); }

    void end_of_elaboration() override
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(m_dev);
        socket.init(sbd, 0);
    }
};

extern "C" void module_register();

#endif
