/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _LIBQBOX_COMPONENTS_DWMAC_STMMAC_H
#define _LIBQBOX_COMPONENTS_DWMAC_STMMAC_H

#include <sstream>
#include <string>

#include <cci_configuration>
#include <module_factory_registery.h>

#include <device.h>
#include <ports/qemu-initiator-signal-socket.h>
#include <ports/target.h>

/*
 * QEMU device "dwmac-stmmac".
 * CCI variant: "xgmac2" (default) or "dw25gmac".
 * CCI properties: mac, variant, netdev, and netdev_str.
 */
class dwmac_stmmac : public QemuDevice
{
protected:
    cci::cci_param<std::string> p_mac;
    cci::cci_param<std::string> p_variant;
    cci::cci_param<std::string> p_netdev;
    cci::cci_param<std::string> p_netdev_str;

public:
    QemuTargetSocket<> regs_socket;
    QemuTargetSocket<> rgmii_socket;
    QemuTargetSocket<> xpcs_socket;
    QemuInitiatorSignalSocket irq_out;

    dwmac_stmmac(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : dwmac_stmmac(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }

    dwmac_stmmac(const sc_core::sc_module_name& n, QemuInstance& inst)
        : QemuDevice(n, inst, "dwmac-stmmac")
        , p_mac("mac", "", "MAC address of NIC; empty generates one")
        , p_variant("variant", "xgmac2", "Controller variant (xgmac2 or dw25gmac)")
        , p_netdev("netdev", std::string(name()) + "-id", "QEMU netdev ID")
        , p_netdev_str("netdev_str", "user", "netdev string for QEMU (do not specify ID)")
        , regs_socket("regs", inst)
        , rgmii_socket("rgmii", inst)
        , xpcs_socket("xpcs", inst)
        , irq_out("irq_out")
    {
        std::stringstream opts;

        opts << p_netdev_str.get_value();
        opts << ",id=" << p_netdev.get_value();
        m_inst.add_arg("-netdev");
        m_inst.add_arg(opts.str().c_str());
    }

    void before_end_of_elaboration() override
    {
        QemuDevice::before_end_of_elaboration();

        if (!p_mac.get_value().empty()) {
            m_dev.set_prop_str("mac", p_mac.get_value().c_str());
        }
        m_dev.set_prop_str("netdev", p_netdev.get_value().c_str());
        m_dev.set_prop_str("variant", p_variant.get_value().c_str());
    }

    void end_of_elaboration() override
    {
        set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(m_dev);

        regs_socket.init(sbd, 0);
        rgmii_socket.init(sbd, 1);
        xpcs_socket.init(sbd, 2);
        irq_out.init_sbd(sbd, 0);
    }
};

extern "C" void module_register();

#endif
