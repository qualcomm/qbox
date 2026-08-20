/*
 * This file is part of libqbox
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * qemu_rciep_pci: QBox wrapper for the QEMU "rciep" PCIe device.
 *
 * Derives from qemu_gpex::Device, registers with the gpex bus, and
 * forwards CCI parameters to the underlying QEMU properties:
 *   devfn          PCI devfn (dev<<3 | fn) → "addr" property
 *   vendor_id      uint16 → "vendor-id"
 *   device_id      uint16 → "device-id"
 *   class_id       uint16 → "class-id"
 *   revision       uint8  → "revision"
 *   num_vfs        uint16 → "num-vfs"
 *   num_msi_vectors uint16 → "num-msi-vectors"
 *   bar0_size      uint64 → "bar0-size"
 *   commblock_base uint64 → "commblock-base"  (BAR0 MMIO forward target)
 *   private_base   uint64 → "private-base"   (reserved — not consumed yet)
 *   pf_shadow_base uint64 → "pf-shadow-base" (reserved — not consumed yet)
 */

#ifndef _LIBQBOX_COMPONENTS_PCI_RCIEP_H
#define _LIBQBOX_COMPONENTS_PCI_RCIEP_H

#include <sstream>
#include <iomanip>

#include <cci_configuration>

#include <libgssync.h>
#include <qemu-instance.h>
#include <module_factory_registery.h>

#include <qemu_gpex.h>

class qemu_rciep_pci : public qemu_gpex::Device
{
public:
    cci::cci_param<uint32_t> p_devfn;
    cci::cci_param<uint32_t> p_vendor_id;
    cci::cci_param<uint32_t> p_device_id;
    cci::cci_param<uint32_t> p_class_id;
    cci::cci_param<uint32_t> p_revision;
    cci::cci_param<uint32_t> p_num_vfs;
    cci::cci_param<uint32_t> p_num_msi_vectors;
    cci::cci_param<uint64_t> p_bar0_size;
    cci::cci_param<uint64_t> p_private_base;
    cci::cci_param<uint64_t> p_pf_shadow_base;
    cci::cci_param<uint64_t> p_commblock_base;

    qemu_rciep_pci(const sc_core::sc_module_name& name, sc_core::sc_object* o, sc_core::sc_object* t)
        : qemu_rciep_pci(name, *(dynamic_cast<QemuInstance*>(o)), (dynamic_cast<qemu_gpex*>(t)))
    {
    }

    qemu_rciep_pci(const sc_core::sc_module_name& name, QemuInstance& inst, qemu_gpex* gpex)
        : qemu_gpex::Device(name, inst, "rciep")
        , p_devfn("devfn", 0, "PCI devfn (dev<<3 | fn)")
        , p_vendor_id("vendor_id", 0x17CB, "PCI Vendor ID")
        , p_device_id("device_id", 0x0D0A, "PCI Device ID")
        , p_class_id("class_id", 0x1200, "PCI class code")
        , p_revision("revision", 0x00, "PCI revision ID")
        , p_num_vfs("num_vfs", 8, "Number of SR-IOV VFs")
        , p_num_msi_vectors("num_msi_vectors", 4,
                            "MSI vectors advertised per PF (keep small vs platform IRQ pool, e.g. GICv2m 64 SPIs)")
        , p_bar0_size("bar0_size", 0x800000, "PF BAR0 MMIO window size in bytes (8 MiB doorbell/comm-block aperture)")
        , p_private_base("private_base", 0, "Reserved — not consumed yet (future CPU-facing PRIVATE window)")
        , p_pf_shadow_base("pf_shadow_base", 0, "Reserved — not consumed yet (future config-monitor mirror)")
        , p_commblock_base("commblock_base", 0,
                           "System-bus base of the external comm-block component (BAR0 forward target)")
    {
        gpex->add_device(*this);
    }

    void before_end_of_elaboration() override
    {
        qemu_gpex::Device::before_end_of_elaboration();

        /* Convert devfn to QEMU "addr" property string: "DD.F" */
        uint32_t devfn = p_devfn.get_value();
        uint32_t dev = (devfn >> 3) & 0x1f;
        uint32_t fn = devfn & 0x7;
        std::ostringstream addr_str;
        addr_str << std::hex << std::setfill('0') << std::setw(2) << dev << "." << fn;
        m_dev.set_prop_str("addr", addr_str.str().c_str());

        m_dev.set_prop_uint("vendor-id", p_vendor_id.get_value());
        m_dev.set_prop_uint("device-id", p_device_id.get_value());
        m_dev.set_prop_uint("class-id", p_class_id.get_value());
        m_dev.set_prop_uint("revision", p_revision.get_value());
        m_dev.set_prop_uint("num-vfs", p_num_vfs.get_value());
        m_dev.set_prop_uint("num-msi-vectors", p_num_msi_vectors.get_value());
        m_dev.set_prop_uint("bar0-size", p_bar0_size.get_value());
        m_dev.set_prop_uint("private-base", p_private_base.get_value());
        m_dev.set_prop_uint("pf-shadow-base", p_pf_shadow_base.get_value());
        m_dev.set_prop_uint("commblock-base", p_commblock_base.get_value());
    }
};

extern "C" void module_register();

#endif /* _LIBQBOX_COMPONENTS_PCI_RCIEP_H */
