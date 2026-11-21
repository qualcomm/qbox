/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _LIBQBOX_COMPONENTS_IRQ_CTRL_CLIC_RISCV_H
#define _LIBQBOX_COMPONENTS_IRQ_CTRL_CLIC_RISCV_H

#include <cci_configuration>

#include <module_factory_registery.h>
#include <device.h>
#include <ports/target.h>
#include <ports/qemu-initiator-signal-socket.h>
#include <ports/qemu-target-signal-socket.h>

class clic_riscv : public QemuDevice
{
public:
    cci::cci_param<unsigned int> p_num_sources;
    cci::cci_param<uint64_t> p_aperture_size;

    QemuTargetSocket<> socket;
    QemuTargetSocket<> interrupt_socket;
    sc_core::sc_vector<QemuTargetSignalSocket> irq_in;
    QemuInitiatorSignalSocket irq_out;

    clic_riscv(const sc_core::sc_module_name& name, sc_core::sc_object* obj)
        : clic_riscv(name, *(dynamic_cast<QemuInstance*>(obj)))
    {
    }

    clic_riscv(sc_core::sc_module_name name, QemuInstance& instance)
        : QemuDevice(name, instance, "riscv.clic")
        , p_num_sources("num_sources", 32, "Number of CLIC sources")
        , p_aperture_size("aperture_size", 0x1000000, "CLIC aperture size")
        , socket("mem", instance)
        , interrupt_socket("mem_interrupt", instance)
        , irq_in("irq_in", p_num_sources, [](const char* n, int) { return new QemuTargetSignalSocket(n); })
        , irq_out("irq_out")
    {
    }

    void before_end_of_elaboration() override
    {
        QemuDevice::before_end_of_elaboration();
        m_dev.set_prop_int("num-sources", p_num_sources);
        m_dev.set_prop_int("aperture-size", p_aperture_size);
    }

    void end_of_elaboration() override
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(get_qemu_dev());
        socket.init(sbd, 0);
        interrupt_socket.init(sbd, 1);
        irq_out.init_sbd(sbd, 0);
        for (unsigned int i = 0; i < p_num_sources; i++) {
            irq_in[i].init(m_dev, i);
        }
    }
};

extern "C" void module_register();

#endif
