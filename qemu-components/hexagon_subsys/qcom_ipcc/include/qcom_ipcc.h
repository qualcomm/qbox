/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_HEXAGON_SUBSYS_QCOM_IPCC_H
#define _LIBQBOX_COMPONENTS_HEXAGON_SUBSYS_QCOM_IPCC_H

#include <module_factory_registery.h>
#include <device.h>
#include <ports/target.h>
#include <ports/qemu-initiator-signal-socket.h>

class qcom_ipcc : public QemuDevice
{
public:
    cci::cci_param<uint32_t> p_num_clients;
    cci::cci_param<uint32_t> p_version;

    QemuTargetSocket<> socket;
    sc_core::sc_vector<QemuInitiatorSignalSocket> irq_out;

    qcom_ipcc(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : qcom_ipcc(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }

    qcom_ipcc(const sc_core::sc_module_name& name, QemuInstance& inst)
        : QemuDevice(name, inst, "qcom-ipcc")
        , p_num_clients("num_clients", 64, "Number of IPCC clients")
        , p_version("version", 0x10200, "IPCC version register value")
        , socket("mem", inst)
        , irq_out("irq_out", p_num_clients, [](const char* n, size_t i) { return new QemuInitiatorSignalSocket(n); })
    {
    }

    void before_end_of_elaboration() override
    {
        QemuDevice::before_end_of_elaboration();
        m_dev.set_prop_int("num-clients", p_num_clients);
        m_dev.set_prop_int("version", p_version);
    }

    void end_of_elaboration() override
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(m_dev);
        socket.init(sbd, 0);
        for (uint32_t i = 0; i < p_num_clients; i++) {
            irq_out[i].init_sbd(sbd, i);
        }
    }
};

extern "C" void module_register();

#endif
