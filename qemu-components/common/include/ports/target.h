/*
 * This file is part of libqbox
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_PORTS_TARGET_H
#define _LIBQBOX_PORTS_TARGET_H

#include <tlm>

#include "qemu-instance.h"
#include "tlm-extensions/qemu-cpu-hint.h"
#include "tlm-extensions/qemu-mr-hint.h"
#include <tlm_sockets_buswidth.h>

class TlmTargetToQemuBridge : public tlm::tlm_fw_transport_if<>
{
public:
    using MemTxAttrs = qemu::MemoryRegion::MemTxAttrs;
    using MemTxResult = qemu::MemoryRegion::MemTxResult;
    using TlmPayload = tlm::tlm_generic_payload;

protected:
    qemu::MemoryRegion m_mr;
    std::shared_ptr<qemu::AddressSpace> m_as;
    QemuInstance* m_inst = nullptr;

    void init_as()
    {
        m_as = m_mr.get_inst().address_space_new();
        m_as->init(m_mr, "qemu-target-socket");
    }

    qemu::Cpu push_current_cpu(TlmPayload& trans)
    {
        qemu::Cpu ret;
        QemuCpuHintTlmExtension* ext = nullptr;

        trans.get_extension(ext);

        if (ext == nullptr) {
            /* return an invalid object */
            return ret;
        }

        qemu::Cpu initiator(ext->get_cpu());

        if (initiator.get_inst_id() != m_mr.get_inst_id()) {
            /* return an invalid object */
            return ret;
        }

        ret = initiator.set_as_current();

        return ret;
    }

    void pop_current_cpu(qemu::Cpu cpu)
    {
        if (!cpu.valid()) {
            return;
        }

        cpu.set_as_current();
    }

public:
    void init(qemu::SysBusDevice sbd, int mmio_idx, QemuInstance& inst)
    {
        m_inst = &inst;
        m_mr = sbd.mmio_get_region(mmio_idx);
        init_as();
    }

    void init_with_mr(qemu::MemoryRegion mr, QemuInstance& inst)
    {
        m_inst = &inst;
        m_mr = mr;
        init_as();
    }

    virtual void b_transport(TlmPayload& trans, sc_core::sc_time& t)
    {
        uint64_t addr = trans.get_address();
        uint64_t* data = reinterpret_cast<uint64_t*>(trans.get_data_ptr());
        unsigned int size = trans.get_data_length();
        MemTxAttrs attrs;
        MemTxResult res;
        qemu::Cpu current_cpu_save;

        if (trans.get_command() == tlm::TLM_IGNORE_COMMAND) {
            trans.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }

        current_cpu_save = push_current_cpu(trans);

        /*
         * QEMU device state (ptimer transactions, virtual clock, etc.) is BQL-protected.
         * SystemC threads reaching this path do not implicitly hold the BQL, so acquire
         * it for the duration of the QEMU dispatch. The BQL is recursive, so this is
         * safe even when the caller (e.g. QemuInitiatorSocket reentrancy>1 path) already
         * holds it.
         */
        m_inst->get().lock_iothread();

        switch (trans.get_command()) {
        case tlm::TLM_READ_COMMAND:
            res = m_as->read(addr, data, size, attrs);
            break;

        case tlm::TLM_WRITE_COMMAND:
            res = m_as->write(addr, data, size, attrs);
            break;

        default:
            /* TLM_IGNORE_COMMAND already handled above */
            m_inst->get().unlock_iothread();
            assert(false);
            return;
        }

        m_inst->get().unlock_iothread();

        trans.set_extension(new QemuMrHintTlmExtension(m_mr, addr));

        switch (res) {
        case qemu::MemoryRegionOps::MemTxOK:
            trans.set_response_status(tlm::TLM_OK_RESPONSE);
            break;

        case qemu::MemoryRegionOps::MemTxDecodeError:
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            break;

        case qemu::MemoryRegionOps::MemTxError:
            trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            break;

        default:
            trans.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
            break;
        }

        pop_current_cpu(current_cpu_save);
    }

    virtual tlm::tlm_sync_enum nb_transport_fw(TlmPayload& trans, tlm::tlm_phase& phase, sc_core::sc_time& t)
    {
        /* TODO: report an error */
        abort();
        return tlm::TLM_ACCEPTED;
    }

    virtual bool get_direct_mem_ptr(TlmPayload& trans, tlm::tlm_dmi& dmi_data) { return false; }

    virtual unsigned int transport_dbg(TlmPayload& trans)
    {
        unsigned int size = trans.get_data_length();
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        b_transport(trans, delay);
        if (trans.get_response_status() == tlm::TLM_OK_RESPONSE)
            return size;
        else
            return 0;
    }
};

template <unsigned int BUSWIDTH = DEFAULT_TLM_BUSWIDTH>
class QemuTargetSocket
    : public tlm::tlm_target_socket<BUSWIDTH, tlm::tlm_base_protocol_types, 1, sc_core::SC_ZERO_OR_MORE_BOUND>
{
public:
    using TlmTargetSocket = tlm::tlm_target_socket<BUSWIDTH, tlm::tlm_base_protocol_types, 1,
                                                   sc_core::SC_ZERO_OR_MORE_BOUND>;
    using TlmPayload = tlm::tlm_generic_payload;

protected:
    TlmTargetToQemuBridge m_bridge;
    QemuInstance& m_inst;
    qemu::SysBusDevice m_sbd;

public:
    QemuTargetSocket(const char* name, QemuInstance& inst): TlmTargetSocket(name), m_inst(inst)
    {
        TlmTargetSocket::bind(m_bridge);
    }

    void init(qemu::SysBusDevice sbd, int mmio_idx) { m_bridge.init(sbd, mmio_idx, m_inst); }

    void init_with_mr(qemu::MemoryRegion mr) { m_bridge.init_with_mr(mr, m_inst); }
};

#endif
