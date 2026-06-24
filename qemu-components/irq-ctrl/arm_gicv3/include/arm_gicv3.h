/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2020
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_IRQ_CTRL_ARM_GICV3_H
#define _LIBQBOX_COMPONENTS_IRQ_CTRL_ARM_GICV3_H

#include <sstream>

#include <systemc>
#include <cci_configuration>

#include <global_peripheral_initiator.h>
#include <libqemu-cxx/libqemu-cxx.h>

#include <device.h>
#include <ports/target.h>
#include <ports/qemu-initiator-signal-socket.h>
#include <ports/qemu-target-signal-socket.h>
#include <qemu-instance.h>
#include <module_factory_registery.h>

class arm_gicv3 : public QemuDevice
{
public:
    static const uint32_t NUM_PPI = 32;

protected:
    cci::cci_param<unsigned int> p_num_cpu;
    cci::cci_param<unsigned int> p_first_cpu_index;
    cci::cci_param<unsigned int> p_num_spi;
    cci::cci_param<unsigned int> p_revision;
    cci::cci_param<std::vector<unsigned int> > p_redist_region;
    cci::cci_param<bool> p_has_security_extensions;
    cci::cci_param<bool> p_has_lpi; // Locality specific Peripheral Interrupts
public:
    QemuTargetSocket<> dist_iface;
    sc_core::sc_vector<QemuTargetSocket<> > redist_iface;

    /* Shared peripheral interrupts: sized with the p_num_spi parameter */
    sc_core::sc_vector<QemuTargetSignalSocket> spi_in;

    /*
     * Private peripheral interrupts: 32 per CPUs, the outer vector is sized with the
     * number of CPUs.
     */
    sc_core::sc_vector<sc_core::sc_vector<QemuTargetSignalSocket> > ppi_in;

    /* Output interrupt lines. Vector are sized with the number of CPUs */
    sc_core::sc_vector<QemuInitiatorSignalSocket> irq_out;
    sc_core::sc_vector<QemuInitiatorSignalSocket> fiq_out;
    sc_core::sc_vector<QemuInitiatorSignalSocket> virq_out;
    sc_core::sc_vector<QemuInitiatorSignalSocket> vfiq_out;
    arm_gicv3(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : arm_gicv3(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }

    static const char* get_gicv3_type(QemuInstance& inst)
    {
        if (inst.is_kvm_enabled()) {
            return "kvm-arm-gicv3";
        } else if (inst.is_whpx_enabled()) {
            return "whpx-arm-gicv3";
        } else {
            return "arm-gicv3";
        }
    }

    arm_gicv3(const sc_core::sc_module_name& name, QemuInstance& inst, unsigned num_cpus = 0)
        : QemuDevice(name, inst, get_gicv3_type(inst))
        , p_num_cpu("num_cpus", num_cpus, "Number of CPU interfaces")
        , p_first_cpu_index("first_cpu_index", 0, "index of the first cpu attached to the GIC (default 0)")
        , p_num_spi("num_spi", 0, "Number of shared peripheral interrupts")
        , p_revision("revision", 3, "Revision of the GIC (3 -> v3, 4 -> v4)")
        // , p_redist_region("redist_region", std::vector<unsigned int>({}),
        //                   "Redistributor regions configuration")
        , p_redist_region("redist_region",
                          std::vector<unsigned int>(gs::cci_get_vector<unsigned int>(
                              cci::cci_get_broker(), std::string(sc_module::name()) + ".redist_region")),
                          "Redistributor regions configuration")
        , p_has_security_extensions("has_security_extensions", false, "Enable security extensions")
        , p_has_lpi("has_lpi", false, "Enable LPI")
        , dist_iface("dist_iface", inst)
        , redist_iface("redist_iface", p_redist_region.get_value().size(),
                       [&inst](const char* n, int i) { return new QemuTargetSocket<>(n, inst); })
        , spi_in("spi_in", p_num_spi)
        , ppi_in("ppi_in_cpu", p_num_cpu,
                 [](const char* n, size_t i) { return new sc_core::sc_vector<QemuTargetSignalSocket>(n, NUM_PPI); })
        , irq_out("irq_out", p_num_cpu)
        , fiq_out("fiq_out", p_num_cpu)
        , virq_out("virq_out", p_num_cpu)
        , vfiq_out("vfiq_out", p_num_cpu)
    {
    }

    void before_end_of_elaboration()
    {
        QemuDevice::before_end_of_elaboration();
        int i;

        m_dev.set_prop_int("num-cpu", p_num_cpu);
        m_dev.set_prop_int("first-cpu-index", p_first_cpu_index);
        m_dev.set_prop_int("num-irq", p_num_spi + NUM_PPI);
        m_dev.set_prop_int("revision", p_revision);

        bool irqchip_in_kernel = m_inst.is_kvm_enabled() || m_inst.is_whpx_enabled();
        bool has_security_extensions = irqchip_in_kernel ? false : p_has_security_extensions.get_value();
        bool has_lpi = irqchip_in_kernel ? false : p_has_lpi.get_value();
        m_dev.set_prop_bool("has-security-extensions", has_security_extensions);
        m_dev.set_prop_bool("has-lpi", has_lpi);
        if (has_lpi) {
            m_dev.set_prop_link("sysmem", *(this->get_qemu_inst().get().get_system_memory()));
        }
        m_dev.set_prop_uint_array("redist-region-count", p_redist_region.get_value());
    }

    void end_of_elaboration()
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(m_dev);
        int cpu, i;

        dist_iface.init(sbd, 0);

        for (i = 0; i < p_redist_region.get_value().size(); i++) {
            redist_iface[i].init(sbd, 1 + i);
        }

        /* SPIs in */
        for (i = 0; i < p_num_spi; i++) {
            spi_in[i].init(m_dev, i);
        }

        /* PPIs in */
        for (cpu = 0; cpu < p_num_cpu; cpu++) {
            for (i = 0; i < NUM_PPI; i++) {
                int ppi_idx = p_num_spi + cpu * NUM_PPI + i;
                ppi_in[cpu][i].init(m_dev, ppi_idx);
            }
        }

        /* Output lines */
        for (cpu = 0; cpu < p_num_cpu; cpu++) {
            irq_out[cpu].init_sbd(sbd, p_num_cpu * 0 + cpu);
            fiq_out[cpu].init_sbd(sbd, p_num_cpu * 1 + cpu);
            virq_out[cpu].init_sbd(sbd, p_num_cpu * 2 + cpu);
            vfiq_out[cpu].init_sbd(sbd, p_num_cpu * 3 + cpu);
        }

        if (m_inst.is_kvm_enabled() || m_inst.is_whpx_enabled()) {
            uint64_t val;
            qemu::MemoryRegionOps::MemTxAttrs attrs = {};
            sc_core::sc_object* init_obj = gs::find_sc_obj(nullptr, "platform.global_peripheral_initiator_arm_0");
            auto init = dynamic_cast<global_peripheral_initiator*>(init_obj);
            assert(init && "Failed to find global peripheral initiator");

            m_inst.get().lock_iothread();
            uint64_t addr;
            addr = gs::cci_get<uint64_t>(cci::cci_get_broker(), std::string(dist_iface.name()) + ".address");
            init->m_initiator.qemu_io_read(addr, &val, sizeof(val), attrs);
            for (i = 0; i < p_redist_region.get_value().size(); i++) {
                addr = gs::cci_get<uint64_t>(cci::cci_get_broker(), std::string(redist_iface[i].name()) + ".address");
                init->m_initiator.qemu_io_read(addr, &val, sizeof(val), attrs);
            }
            m_inst.get().unlock_iothread();
        }
    }
};

extern "C" void module_register();
#endif
