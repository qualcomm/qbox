/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _LIBQBOX_COMPONENTS_UFS_H
#define _LIBQBOX_COMPONENTS_UFS_H

#include <systemc>
#include <cci_configuration>
#include <module_factory_registery.h>
#include <device.h>
#include <ports/target.h>
#include <ports/initiator.h>
#include <ports/qemu-initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <tlm_sockets_buswidth.h>
#include <vector>

class ufs : public QemuDevice, public QemuInitiatorIface
{
public:
    class Device : public QemuDevice
    {
        friend ufs;

    public:
        Device(const sc_core::sc_module_name& name, QemuInstance& inst, const char* qom_type)
            : QemuDevice(name, inst, qom_type)
        {
        }

        std::function<void()> set_dev_props;

        /// @brief We cannot do the end_of_elaboration at this point because we
        /// need the UFS bus (created only during the UFS host realize step)
        void end_of_elaboration() override {}

    protected:
        virtual void ufs_realize(qemu::Bus& ufs_bus)
        {
            m_dev.set_parent_bus(ufs_bus);
            QemuDevice::end_of_elaboration();
        }
    };

public:
    QemuTargetSocket<> q_socket;
    QemuInitiatorSignalSocket irq_out;
    /*
     * Optional DMA initiator. When bound (e.g. to an SMMU TBU upstream
     * socket), all controller DMA (UTRD/UCD/PRDT data) is issued through it
     * so that device IOVAs are translated. When unbound, DMA goes to QEMU
     * system memory as before.
     */
    QemuInitiatorSocket<> dma_socket;
    /*
     * Platform reset. Drops every DMI/IOMMU mapping cached by the DMA
     * initiator so that DMA after reboot is re-translated against the reset
     * SMMU state. (QEMU device state itself is reset by the QEMU system
     * reset.)
     */
    TargetSignalSocket<bool> reset;

protected:
    cci::cci_param<std::string> p_serial;
    cci::cci_param<uint8_t> p_nutrs;
    cci::cci_param<uint8_t> p_nutmrs;
    cci::cci_param<bool> p_mcq;
    cci::cci_param<uint8_t> p_mcq_maxq;
    std::vector<Device*> m_devices;

public:
    ufs(const sc_core::sc_module_name& name, sc_core::sc_object* o): ufs(name, *(dynamic_cast<QemuInstance*>(o))) {}
    ufs(const sc_core::sc_module_name& n, QemuInstance& inst)
        : QemuDevice(n, inst, "ufs-sysbus")
        , q_socket("mem", inst)
        , irq_out("irq_out")
        , dma_socket("dma", *this, inst)
        , reset("reset")
        , p_serial("serial", "", "controller serial")
        , p_nutrs("nutrs", 32, "number of UTP transfer request slots")
        , p_nutmrs("nutmrs", 8, "number of UTP task management request slots")
        , p_mcq("mcq", false, "multiple command queue support")
        , p_mcq_maxq("mcq_maxq", 2, "MCQ maximum number of queues")
    {
        set_id(name());
        reset.register_value_changed_cb([this](bool value) {
            if (value && dma_socket.size() > 0) {
                m_inst.get().lock_iothread();
                dma_socket.reset();
                m_inst.get().unlock_iothread();
            }
        });
    }

    void add_device(Device& dev)
    {
        if (m_inst != dev.get_qemu_inst()) {
            SCP_FATAL(SCMOD) << "UFS device and host have to be in same qemu instance";
        }
        m_devices.push_back(&dev);
    }

    void before_end_of_elaboration() override
    {
        QemuDevice::before_end_of_elaboration();
        if (!p_serial.get_value().empty()) {
            m_dev.set_prop_str("serial", p_serial.get_value().c_str());
        }
        if (!p_nutrs.is_default_value()) m_dev.set_prop_uint("nutrs", p_nutrs.get_value());
        if (!p_nutmrs.is_default_value()) m_dev.set_prop_uint("nutmrs", p_nutmrs.get_value());
        if (!p_mcq.is_default_value()) m_dev.set_prop_bool("mcq", p_mcq.get_value());
        if (!p_mcq_maxq.is_default_value()) m_dev.set_prop_uint("mcq-maxq", p_mcq_maxq.get_value());
        if (dma_socket.size() > 0) {
            dma_socket.init(m_dev, "dma");
        }
    }

    void end_of_elaboration() override
    {
        QemuDevice::set_sysbus_as_parent_bus();
        QemuDevice::end_of_elaboration();
        qemu::SysBusDevice sbd(m_dev);
        q_socket.init(sbd, 0);
        irq_out.init_sbd(sbd, 0);

        qemu::Bus root_bus = m_dev.get_child_bus((std::string(name()) + ".0").c_str());
        for (Device* device : m_devices) {
            if (device->set_dev_props) {
                device->set_dev_props();
            }
            device->ufs_realize(root_bus);
        }
    }

    /* QemuInitiatorIface */
    void initiator_customize_tlm_payload(TlmPayload& payload) override {}
    void initiator_tidy_tlm_payload(TlmPayload& payload) override {}
    sc_core::sc_time initiator_get_local_time() override { return sc_core::sc_time_stamp(); }
    void initiator_set_local_time(const sc_core::sc_time& t) override {}
    void initiator_async_run(qemu::Cpu::AsyncJobFn job) override {}
};

extern "C" void module_register();

#endif
