/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_HEXAGON_SUBSYS_QCOM_TCSR_H
#define _LIBQBOX_COMPONENTS_HEXAGON_SUBSYS_QCOM_TCSR_H

#include <vector>

#include <module_factory_registery.h>
#include <device.h>
#include <ports/target.h>

class qcom_tcsr : public QemuDevice
{
public:
    cci::cci_param<uint32_t> p_family_number;
    cci::cci_param<uint32_t> p_device_number;
    cci::cci_param<uint32_t> p_major_version;
    cci::cci_param<uint32_t> p_minor_version;
    cci::cci_param<uint32_t> p_emulation_type;

    QemuTargetSocket<> socket;

    std::vector<unsigned int> m_tz_wonce_init;

    qcom_tcsr(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : qcom_tcsr(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }

    qcom_tcsr(const sc_core::sc_module_name& name, QemuInstance& inst)
        : QemuDevice(name, inst, "tcsr")
        , p_family_number("family_number", 0xE, "SOC family number")
        , p_device_number("device_number", 0x875, "SOC device number")
        , p_major_version("major_version", 0x1, "SOC major version")
        , p_minor_version("minor_version", 0x0, "SOC minor version")
        , p_emulation_type("emulation_type", 0x0, "SOC emulation type")
        , socket("mem", inst)
    {
    }

    void before_end_of_elaboration() override
    {
        QemuDevice::before_end_of_elaboration();
        m_dev.set_prop_int("family-number", p_family_number);
        m_dev.set_prop_int("device-number", p_device_number);
        m_dev.set_prop_int("major-version", p_major_version);
        m_dev.set_prop_int("minor-version", p_minor_version);
        m_dev.set_prop_int("emulation-type", p_emulation_type);
        if (!m_tz_wonce_init.empty()) {
            m_dev.set_prop_uint_array("tz-wonce-init", m_tz_wonce_init);
        }
    }

    void end_of_elaboration() override
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(m_dev);
        socket.init(sbd, 0);
    }
};

extern "C" void module_register();

#endif
