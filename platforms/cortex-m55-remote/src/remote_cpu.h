/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* Quic Module Cortex-M55 */
#ifndef __REMOTE_CORTEX_M55__
#define __REMOTE_CORTEX_M55__

#include <systemc>
#include <tlm>
#include <cci_configuration>
#include <libgsutils.h>
#include <limits>
#include <cpu_arm/cpu_arm_cortex_m55/include/cortex-m55.h>
#include <qemu-instance.h>
#include "gs_memory/include/gs_memory.h"
#include "router/include/router.h"
#include "remote.h"
#include "pass/include/pass.h"
#include <module_factory_registery.h>

class RemoteCPU : public sc_core::sc_module
{
    SCP_LOGGER();

public:
    RemoteCPU(const sc_core::sc_module_name& n, sc_core::sc_object* obj)
        : RemoteCPU(n, *(dynamic_cast<QemuInstance*>(obj)))
    {
    }
    RemoteCPU(const sc_core::sc_module_name& n, QemuInstance& qemu_inst)
        : sc_core::sc_module(n)
        , m_broker(cci::cci_get_broker())
        , m_gdb_port("gdb_port", 0, "GDB port")
        , m_qemu_inst(qemu_inst)
        , m_router("router")
        , m_nvic("nvic", m_qemu_inst)
        , m_cpu("cpu", m_qemu_inst, m_nvic)
    {
        unsigned int m_irq_num = m_broker.get_param_handle(std::string(this->name()) + ".nvic.num_irq")
                                     .get_cci_value()
                                     .get_uint();

        if (!m_gdb_port.is_default_value()) m_cpu.p_gdb_port = m_gdb_port;

        SCP_INFO(()) << "number of irqs  = " << m_irq_num;

        m_router.initiator_socket.bind(m_nvic.socket);
        m_cpu.socket.bind(m_router.target_socket);
    }

private:
    cci::cci_broker_handle m_broker;
    cci::cci_param<int> m_gdb_port;
    QemuInstance& m_qemu_inst;
    gs::router<> m_router;
    nvic_armv7m m_nvic;
    cpu_arm_cortexM55 m_cpu;
};
GSC_MODULE_REGISTER(RemoteCPU, sc_core::sc_object*);
#endif
