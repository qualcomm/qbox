/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_COMPONENTS_HEXAGON_SUBSYS_QCOM_TURING_RSC_H
#define _LIBQBOX_COMPONENTS_HEXAGON_SUBSYS_QCOM_TURING_RSC_H

#include <module_factory_registery.h>
#include <device.h>
#include <ports/target.h>
#include <ports/qemu-initiator-signal-socket.h>

class qcom_turing_rsc : public QemuDevice
{
public:
    cci::cci_param<uint32_t> p_rsc_id_reset;
    cci::cci_param<uint32_t> p_solver_config_reset;
    cci::cci_param<uint32_t> p_rsc_config_reset;
    cci::cci_param<uint32_t> p_parentchild_config_reset;
    cci::cci_param<uint32_t> p_cmd_spacing;
    cci::cci_param<uint32_t> p_tcs_base_offset;
    cci::cci_param<uint32_t> p_cmd_base_in_tcs;
    cci::cci_param<uint32_t> p_tcs_timeout_base;
    cci::cci_param<uint32_t> p_timeout_clr_offset;
    cci::cci_param<uint32_t> p_timeout_status_offset;
    cci::cci_param<uint32_t> p_timeout_val_offset;
    cci::cci_param<uint32_t> p_num_br_addr;
    cci::cci_param<uint32_t> p_num_timestamp_units;

    QemuTargetSocket<> socket;
    QemuInitiatorSignalSocket error_irq;
    QemuInitiatorSignalSocket amc_irq;

    qcom_turing_rsc(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : qcom_turing_rsc(name, *(dynamic_cast<QemuInstance*>(o)))
    {
    }

    qcom_turing_rsc(const sc_core::sc_module_name& name, QemuInstance& inst)
        : QemuDevice(name, inst, "turing-rsc")
        , p_rsc_id_reset("rsc_id_reset", 0x00020400, "rsc-id-reset (SA8775P CDSP0 default)")
        , p_solver_config_reset("solver_config_reset", 0x00010100, "solver-config-reset (SA8775P CDSP0 default)")
        , p_rsc_config_reset("rsc_config_reset", 0x01300214, "rsc-config-reset (SA8775P CDSP0 default)")
        , p_parentchild_config_reset("parentchild_config_reset", 0x8000000AU,
                                     "parentchild-config-reset (SA8775P CDSP0 default)")
        , p_cmd_spacing("cmd_spacing", 0x14, "cmd-spacing (SA8775P CDSP0 default)")
        , p_tcs_base_offset("tcs_base_offset", 0x10, "tcs-base-offset (SA8775P CDSP0 default)")
        , p_cmd_base_in_tcs("cmd_base_in_tcs", 0x14, "cmd-base-in-tcs (SA8775P CDSP0 default)")
        , p_tcs_timeout_base("tcs_timeout_base", 0x3D44, "tcs-timeout-base (SA8775P CDSP0 default)")
        , p_timeout_clr_offset("timeout_clr_offset", 0x04, "timeout-clr-offset (SA8775P CDSP0 default)")
        , p_timeout_status_offset("timeout_status_offset", 0x08, "timeout-status-offset (SA8775P CDSP0 default)")
        , p_timeout_val_offset("timeout_val_offset", 0x0C, "timeout-val-offset (SA8775P CDSP0 default)")
        , p_num_br_addr("num_br_addr", 4, "num-br-addr (SA8775P CDSP0 default)")
        , p_num_timestamp_units("num_timestamp_units", 6, "num-timestamp-units (SA8775P CDSP0 default)")
        , socket("mem", inst)
        , error_irq("error_irq")
        , amc_irq("amc_irq")
    {
    }

    void before_end_of_elaboration() override
    {
        QemuDevice::before_end_of_elaboration();
        m_dev.set_prop_int("rsc-id-reset", p_rsc_id_reset);
        m_dev.set_prop_int("solver-config-reset", p_solver_config_reset);
        m_dev.set_prop_int("rsc-config-reset", p_rsc_config_reset);
        m_dev.set_prop_int("parentchild-config-reset", p_parentchild_config_reset);
        m_dev.set_prop_int("cmd-spacing", p_cmd_spacing);
        m_dev.set_prop_int("tcs-base-offset", p_tcs_base_offset);
        m_dev.set_prop_int("cmd-base-in-tcs", p_cmd_base_in_tcs);
        m_dev.set_prop_int("tcs-timeout-base", p_tcs_timeout_base);
        m_dev.set_prop_int("timeout-clr-offset", p_timeout_clr_offset);
        m_dev.set_prop_int("timeout-status-offset", p_timeout_status_offset);
        m_dev.set_prop_int("timeout-val-offset", p_timeout_val_offset);
        m_dev.set_prop_int("num-br-addr", p_num_br_addr);
        m_dev.set_prop_int("num-timestamp-units", p_num_timestamp_units);
    }

    void end_of_elaboration() override
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();

        qemu::SysBusDevice sbd(m_dev);
        socket.init(sbd, 0);
        error_irq.init_sbd(sbd, 0);
        amc_irq.init_sbd(sbd, 1);
    }
};

extern "C" void module_register();

#endif
