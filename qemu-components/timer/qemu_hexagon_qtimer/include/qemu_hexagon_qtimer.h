/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_TIMER_HEXAGON_QTIMER_H
#define _LIBQBOX_COMPONENTS_TIMER_HEXAGON_QTIMER_H

#include <string>
#include <cassert>
#include <memory>
#include <vector>

#include <device.h>
#include <ports/target.h>
#include <ports/qemu-target-signal-socket.h>
#include <ports/qemu-initiator-signal-socket.h>
#include <module_factory_registery.h>

class qemu_hexagon_qtimer : public QemuDevice
{
protected:
    cci::cci_param<unsigned int> p_nr_frames;
    cci::cci_param<unsigned int> p_nr_views;
    cci::cci_param<unsigned int> p_frame_stride;
    cci::cci_param<uint32_t> p_freq_hz;
    cci::cci_param<uint32_t> p_freq_scale;
    /* One cnttid register covers 8 frames (4 bits/nibble per frame in a 32-bit register).
     * Number of cnttid registers = ceil(nr_frames / 8). */
    std::vector<std::unique_ptr<cci::cci_param<unsigned int>>> p_cnttid;

public:
    QemuTargetSocket<> socket;

    /*
     * FIXME:
     * the irq is not really declared as a gpio in qemu, so we cannot
     * import it
     *QemuInitiatorSignalSocket irq;
     */

    /* timers mem/irq */
    QemuTargetSocket<> view_socket;
    sc_core::sc_vector<QemuInitiatorSignalSocket> irq;

public:
    qemu_hexagon_qtimer(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : qemu_hexagon_qtimer(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }
    qemu_hexagon_qtimer(sc_core::sc_module_name nm, QemuInstance& inst)
        : QemuDevice(nm, inst, "qct-qtimer")
        , p_nr_frames("nr_frames", 2, "Number of frames")
        , p_nr_views("nr_views", 1, "Number of views")
        , p_frame_stride("frame_stride", 0x1000, "Frame stride in bytes")
        , p_freq_hz("freq_hz", 19200000, "freq of the timer in hz")
        , p_freq_scale("freq_scale", 100, "scale down factor of the timer freq")
        , socket("mem", inst)
        , view_socket("mem_view", inst)
        , irq("irq", p_nr_frames.get_value(), [](const char* n, size_t i) { return new QemuInitiatorSignalSocket(n); })
    {
        /* 4 bits (one nibble) per frame in a 32-bit cnttid register -> 8 frames per register */
        unsigned int nr_cnttid = (p_nr_frames.get_value() + 7) / 8;
        for (unsigned int i = 0; i < nr_cnttid; ++i) {
            p_cnttid.push_back(std::make_unique<cci::cci_param<unsigned int>>(
                std::string("cnttid_") + std::to_string(i), (i == 0) ? 0x11u : 0x0u,
                std::string("Value of cnttid_") + std::to_string(i)));
        }
    }

    void before_end_of_elaboration() override
    {
        QemuDevice::before_end_of_elaboration();

        m_dev.set_prop_int("nr_frames", p_nr_frames);
        m_dev.set_prop_int("nr_views", p_nr_views);
        m_dev.set_prop_int("frame_stride", p_frame_stride);
        m_dev.set_prop_int("freq", p_freq_hz.get_value());
        m_dev.set_prop_int("freq-scale", p_freq_scale.get_value());
        for (unsigned int i = 0; i < p_cnttid.size(); ++i) {
            m_dev.set_prop_int((std::string("cnttid_") + std::to_string(i)).c_str(), *p_cnttid[i]);
        }
    }

    void end_of_elaboration() override
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(m_dev);
        socket.init(sbd, 0);
        view_socket.init(sbd, 1);

        for (uint32_t i = 0; i < p_nr_frames.get_value(); ++i) {
            irq[i].init_sbd(sbd, i);
        }
    }
};

extern "C" void module_register();
#endif
