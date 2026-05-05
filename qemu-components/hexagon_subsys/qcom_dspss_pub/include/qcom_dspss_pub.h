/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_HEXAGON_SUBSYS_QCOM_DSPSS_PUB_H
#define _LIBQBOX_COMPONENTS_HEXAGON_SUBSYS_QCOM_DSPSS_PUB_H

#include <module_factory_registery.h>
#include <device.h>
#include <ports/target.h>
#include <ports/qemu-initiator-signal-socket.h>

class qcom_dspss_pub : public QemuDevice
{
public:
    QemuTargetSocket<> socket;
    QemuInitiatorSignalSocket nmi_out;
    QemuInitiatorSignalSocket hex_halt_out;

    qcom_dspss_pub(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : qcom_dspss_pub(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }

    qcom_dspss_pub(const sc_core::sc_module_name& name, QemuInstance& inst)
        : QemuDevice(name, inst, "dspss_pub"), socket("mem", inst), nmi_out("nmi_out"), hex_halt_out("hex_halt_out")
    {
    }

    void before_end_of_elaboration() override { QemuDevice::before_end_of_elaboration(); }

    void end_of_elaboration() override
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(m_dev);
        socket.init(sbd, 0);
        nmi_out.init_named(m_dev, "nmi", 0);
        hex_halt_out.init_named(m_dev, "hex-halt", 0);
    }
};

extern "C" void module_register();

#endif
