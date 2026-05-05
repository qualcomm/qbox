/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_HEXAGON_SUBSYS_QCOM_CDSP0_CLKCTL_H
#define _LIBQBOX_COMPONENTS_HEXAGON_SUBSYS_QCOM_CDSP0_CLKCTL_H

#include <module_factory_registery.h>
#include <device.h>
#include <ports/target.h>

class qcom_cdsp0_clkctl : public QemuDevice
{
public:
    QemuTargetSocket<> socket;

    qcom_cdsp0_clkctl(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : qcom_cdsp0_clkctl(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }

    qcom_cdsp0_clkctl(const sc_core::sc_module_name& name, QemuInstance& inst)
        : QemuDevice(name, inst, "cdsp0-clkctl"), socket("mem", inst)
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
