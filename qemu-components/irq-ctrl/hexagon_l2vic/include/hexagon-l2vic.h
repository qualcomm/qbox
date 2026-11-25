/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_IRQ_CTRL_HEXAGON_L2VIC_H
#define _LIBQBOX_COMPONENTS_IRQ_CTRL_HEXAGON_L2VIC_H

#include <string>
#include <cassert>

#include <module_factory_registery.h>
#include <device.h>

#include <ports/target.h>
#include <ports/qemu-target-signal-socket.h>
#include <ports/qemu-initiator-signal-socket.h>

class hexagon_l2vic : public QemuDevice
{
public:
    const unsigned int p_num_sources;
    const unsigned int p_num_outputs;

    QemuTargetSocket<> socket;
    QemuTargetSocket<> socket_fast;
    sc_core::sc_vector<QemuTargetSignalSocket> irq_in;
    sc_core::sc_vector<QemuInitiatorSignalSocket> irq_out;
    QemuTargetSignalSocket reset;

    hexagon_l2vic(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : hexagon_l2vic(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }
    hexagon_l2vic(sc_core::sc_module_name nm, QemuInstance& inst)
        : QemuDevice(nm, inst, "hex-l2vic")
        , p_num_sources(1024) /* this is hardcoded in qemu */
        , p_num_outputs(8)    /* this is hardcoded in qemu */
        , socket("mem", inst)
        , socket_fast("fastmem", inst)
        , irq_in("irq_in", p_num_sources, [](const char* n, int i) { return new QemuTargetSignalSocket(n); })
        , irq_out("irq_out", p_num_outputs, [](const char* n, int i) { return new QemuInitiatorSignalSocket(n); })
        , reset("reset")
    {
    }

    void before_end_of_elaboration() override { QemuDevice::before_end_of_elaboration(); }

    void end_of_elaboration() override
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(m_dev);
        socket.init(sbd, 0);
        socket_fast.init(sbd, 1);

        for (int i = 0; i < p_num_sources; i++) {
            irq_in[i].init(m_dev, i);
        }
        reset.init_named(m_dev, "reset", 0);
        for (int i = 0; i < p_num_outputs; i++) {
            irq_out[i].init_sbd(sbd, i);
        }
    }
};

extern "C" void module_register();

#endif
