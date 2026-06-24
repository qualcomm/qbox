/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _LIBQBOX_COMPONENTS_E1000E_PCI_H
#define _LIBQBOX_COMPONENTS_E1000E_PCI_H

#include <cci_configuration>

#include <libgssync.h>
#include <qemu-instance.h>

#include <module_factory_registery.h>

#include <qemu_gpex.h>

// Wraps QEMU's "e1000e" PCIe NIC (Intel 82574L) so it can be instantiated
// as a qbox PCI device. Same boilerplate as virtio_net_pci / rtl8139_pci;
// the only thing that differs is the QEMU device-model name string passed
// to qemu_gpex::Device.
//
// Why this NIC: kdnet (Windows kernel network debugging transport) ships
// with native support for a fixed set of vendor/device IDs, and e1000e
// (8086:10D3) is on that list. virtio-net is not, which makes it unusable
// for kdnet without a custom extensibility module.
class e1000e_pci : public qemu_gpex::Device
{
    std::string m_netdev_id;
    cci::cci_param<std::string> p_mac;
    cci::cci_param<std::string> p_netdev_str;
    // PCI slot pinning. Empty = auto-assign by gpex. Useful when the
    // guest OS records a specific bus/device/function in persistent state
    // (e.g. Windows kdnet busparams in BCD, NVMe boot device records) and
    // re-assignment causes mismatches that break the guest. Format is
    // "<dev>.<func>" or just "<dev>"; QEMU's addr qdev prop takes that
    // string verbatim.
    cci::cci_param<std::string> p_addr;

public:
    e1000e_pci(const sc_core::sc_module_name& name, sc_core::sc_object* o, sc_core::sc_object* t)
        : e1000e_pci(name, *(dynamic_cast<QemuInstance*>(o)), (dynamic_cast<qemu_gpex*>(t)))
    {
    }
    e1000e_pci(const sc_core::sc_module_name& name, QemuInstance& inst, qemu_gpex* gpex)
        : qemu_gpex::Device(name, inst, "e1000e")
        , m_netdev_id(std::string(sc_core::sc_module::name()) + "-id")
        , p_mac("mac", "", "MAC address of NIC")
        , p_netdev_str("netdev_str", "type=user", "netdev string for QEMU (do not specify ID)")
        , p_addr("addr", "", "PCI slot pinning, e.g. \"02.0\"; empty leaves it to gpex auto-assignment")
    {
        std::stringstream opts;
        opts << p_netdev_str.get_value();
        opts << ",id=" << m_netdev_id;

        m_inst.add_arg("-netdev");
        m_inst.add_arg(opts.str().c_str());

        gpex->add_device(*this);
    }

    void before_end_of_elaboration() override
    {
        qemu_gpex::Device::before_end_of_elaboration();
        if (!p_mac.get_value().empty()) m_dev.set_prop_str("mac", p_mac.get_value().c_str());
        m_dev.set_prop_str("netdev", m_netdev_id.c_str());
        m_dev.set_prop_str("romfile", "");
        if (!p_addr.get_value().empty()) m_dev.set_prop_str("addr", p_addr.get_value().c_str());
    }
};

extern "C" void module_register();

#endif
