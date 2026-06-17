/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _LIBQBOX_COMPONENTS_IRQ_CTRL_APLIC_SIFIVE_H
#define _LIBQBOX_COMPONENTS_IRQ_CTRL_APLIC_SIFIVE_H

#include <string>
#include <cassert>

#include <cci_configuration>

#include <libqemu-cxx/target/riscv.h>
#include <module_factory_registery.h>
#include <device.h>

#include <ports/target.h>
#include <ports/qemu-target-signal-socket.h>
#include <ports/qemu-initiator-signal-socket.h>

/*
 * aplic_sifive — QBox wrapper for the libqemu "riscv.aplic" device (RISC-V AIA
 * Advanced Platform-Level Interrupt Controller).
 *
 * Direct-delivery mode (msimode=false): IRQ sources enter on irq_in[0..num_irqs-1]
 * (gpio_in); one irq_out per hart drives MEIP (mmode=true) or SEIP (mmode=false).
 *
 * Not yet modelled: MSI mode (needs an IMSIC peer; irq_out stays empty) and the
 * M/S APLIC domain hierarchy (riscv_aplic_add_child).
 */
class aplic_sifive : public QemuDevice
{
public:
    /* CCI parameters — map 1:1 to libqemu "riscv.aplic" device properties. */

    /* APLIC register window size in bytes; APLIC_SIZE(n) = 0x4000 + align32(32 * n). */
    cci::cci_param<uint64_t> p_aperture_size;
    /* Hart ID of the first hart served by this domain. */
    cci::cci_param<unsigned int> p_hartid_base;
    /* Number of harts served by this domain. */
    cci::cci_param<unsigned int> p_num_harts;
    /* Interrupt-priority mask = (1 << iprio_bits) - 1 (e.g. 0xFF for 8-bit). */
    cci::cci_param<unsigned int> p_iprio_mask;
    /* Number of interrupt sources (libqemu stores num_sources+1; +1 added below). */
    cci::cci_param<unsigned int> p_num_irqs;
    /* false = direct-delivery (MEIP/SEIP); true = MSI mode (needs IMSIC). */
    cci::cci_param<bool> p_msimode;
    /* true = M-mode domain (MEIP); false = S-mode domain (SEIP). */
    cci::cci_param<bool> p_mmode;

    /* Ports */

    /* APLIC register window (sysbus MMIO index 0). */
    QemuTargetSocket<> socket;
    /* IRQ sources (gpio_in, 0..num_irqs-1); root domain only. */
    sc_core::sc_vector<QemuTargetSignalSocket> irq_in;
    /* Per-hart outputs (gpio_out) to MEIP/SEIP; empty in MSI mode. */
    sc_core::sc_vector<QemuInitiatorSignalSocket> irq_out;

    aplic_sifive(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : aplic_sifive(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }

    aplic_sifive(sc_core::sc_module_name nm, QemuInstance& inst)
        : QemuDevice(nm, inst, "riscv.aplic")
        , p_aperture_size("aperture_size", 0, "APLIC register window size in bytes")
        , p_hartid_base("hartid_base", 0, "Hart ID of the first hart in this domain")
        , p_num_harts("num_harts", 0, "Number of harts served by this domain")
        , p_iprio_mask("iprio_mask", 0xFF, "Interrupt-priority mask = (1 << iprio_bits) - 1")
        , p_num_irqs("num_irqs", 0, "Number of interrupt sources")
        , p_msimode("msimode", false, "false = direct (MEIP/SEIP); true = MSI mode")
        , p_mmode("mmode", true, "true = M-mode (MEIP); false = S-mode (SEIP)")
        , socket("mem", inst)
        , irq_in("irq_in", p_num_irqs, [](const char* n, int) { return new QemuTargetSignalSocket(n); })
        , irq_out("irq_out", p_num_harts, [](const char* n, int) { return new QemuInitiatorSignalSocket(n); })
    {
    }

    void before_end_of_elaboration() override
    {
        QemuDevice::before_end_of_elaboration();

        m_dev.set_prop_int("aperture-size", p_aperture_size);
        m_dev.set_prop_int("hartid-base", p_hartid_base);
        m_dev.set_prop_int("num-harts", p_num_harts);
        m_dev.set_prop_int("iprio-mask", p_iprio_mask);
        m_dev.set_prop_int("num-irqs", p_num_irqs + 1); /* libqemu stores num_sources+1 */
        m_dev.set_prop_bool("msimode", p_msimode);
        m_dev.set_prop_bool("mmode", p_mmode);
    }

    void end_of_elaboration() override
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(get_qemu_dev());

        socket.init(sbd, 0);

        /* IRQ sources (gpio_in); root domain only. */
        for (unsigned int i = 0; i < p_num_irqs; i++) {
            irq_in[i].init(m_dev, i);
        }

        /*
         * Per-hart outputs. riscv.aplic exposes these as gpio-out
         * (qdev_init_gpio_out), not sysbus IRQs — so use init(m_dev, i), not
         * init_sbd(). Skipped in MSI mode, where no gpio_out lines are created.
         */
        if (!p_msimode) {
            for (unsigned int i = 0; i < p_num_harts; i++) {
                irq_out[i].init(m_dev, i);
            }
        }
    }
};

extern "C" void module_register();

#endif /* _LIBQBOX_COMPONENTS_IRQ_CTRL_APLIC_SIFIVE_H */
