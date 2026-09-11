/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _GREENSOCS_BASE_COMPONENTS_ADDRTR_H
#define _GREENSOCS_BASE_COMPONENTS_ADDRTR_H

#include <algorithm>
#include <cinttypes>
#include <limits>
#include <vector>

#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <cci_configuration>
#include <scp/report.h>

#include <libgsutils.h>
#include <module_factory_registery.h>
#include <tlm_sockets_buswidth.h>

/**
 * @class Addrtr
 *
 * @brief A Addrtr component that can add addrtr to a virtual platform project to manage the various
 * transactions
 *
 * @details This component models a addrtr. It has a single target socket so another
 * component with an initiator socket can connect to this component. It behaves as follows:
 * translate the target_socket address to the mapped_base_addr (CCI parameter) and reinitiate the transaction
 * using the initiator_socket.
 */

class addrtr : public sc_core::sc_module
{
private:
    SCP_LOGGER();

    bool range_end(sc_dt::uint64 start, sc_dt::uint64 size, sc_dt::uint64& end) const
    {
        if (size == 0 || start > std::numeric_limits<sc_dt::uint64>::max() - (size - 1)) {
            return false;
        }
        end = start + size - 1;
        return true;
    }

    bool map_window(sc_dt::uint64& start_addr, sc_dt::uint64& end_addr, unsigned char** dmi_ptr = nullptr)
    {
        sc_dt::uint64 mapped_end;
        sc_dt::uint64 target_end;
        const sc_dt::uint64 mapped_start = p_mapped_base_addr.get_value();
        const sc_dt::uint64 target_start = m_base_addr;

        if (!range_end(mapped_start, m_mapping_size, mapped_end) ||
            !range_end(target_start, m_mapping_size, target_end) || end_addr < start_addr || end_addr < mapped_start ||
            start_addr > mapped_end) {
            return false;
        }

        const sc_dt::uint64 clipped_start = std::max(start_addr, mapped_start);
        const sc_dt::uint64 clipped_end = std::min(end_addr, mapped_end);

        if (dmi_ptr != nullptr && *dmi_ptr != nullptr) {
            *dmi_ptr += clipped_start - start_addr;
        }

        start_addr = target_start + (clipped_start - mapped_start);
        end_addr = target_start + (clipped_end - mapped_start);
        return true;
    }

    sc_dt::uint64 addr_fw(sc_dt::uint64 addr)
    {
        auto offset = addr - m_base_addr;
        return p_mapped_base_addr.get_value() + offset;
    }

    sc_dt::uint64 addr_bw(sc_dt::uint64 addr)
    {
        auto offset = addr - p_mapped_base_addr.get_value();
        return m_base_addr + offset;
    }

    sc_dt::uint64 map_txn_addr(tlm::tlm_generic_payload& trans)
    {
        sc_dt::uint64 addr = trans.get_address();
        sc_dt::uint64 len = trans.get_data_length();
        sc_dt::uint64 mapped_addr = addr;
        sc_dt::uint64 target_end;
        sc_dt::uint64 mapped_end;
        sc_dt::uint64 txn_end = addr;
        const bool range_valid = range_end(m_base_addr, m_mapping_size, target_end);
        const bool mapped_range_valid = range_end(p_mapped_base_addr.get_value(), m_mapping_size, mapped_end);
        const bool txn_valid = (len == 0) || range_end(addr, len, txn_end);
        if (range_valid && mapped_range_valid && txn_valid && addr >= m_base_addr && txn_end <= target_end) {
            mapped_addr = addr_fw(addr);
            trans.set_address(mapped_addr);
        } else {
            SCP_FATAL(()) << "The txn [addr: 0x" << std::hex << addr << "] len: 0x" << std::hex << len
                          << ", doesn't belong to the target_socket base address: 0x" << std::hex << m_base_addr
                          << " size: 0x" << std::hex << m_mapping_size;
        }
        return mapped_addr;
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        sc_dt::uint64 orig_addr = trans.get_address();
        sc_dt::uint64 mapped_addr = map_txn_addr(trans);
        SCP_DEBUG(()) << "b_transport to addr: 0x" << std::hex << orig_addr << " will be mapped to addr: 0x" << std::hex
                      << mapped_addr;
        initiator_socket->b_transport(trans, delay);
        trans.set_address(orig_addr);
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        sc_dt::uint64 orig_addr = trans.get_address();
        sc_dt::uint64 mapped_addr = map_txn_addr(trans);
        SCP_DEBUG(()) << "transport_dbg to addr: 0x" << std::hex << orig_addr << " will be mapped to addr: 0x"
                      << std::hex << mapped_addr;
        unsigned int ret = initiator_socket->transport_dbg(trans);
        trans.set_address(orig_addr);
        return ret;
    }

    bool get_direct_mem_ptr(tlm::tlm_generic_payload& trans, tlm::tlm_dmi& dmi_data)
    {
        sc_dt::uint64 orig_addr = trans.get_address();
        sc_dt::uint64 mapped_addr = map_txn_addr(trans);
        bool ret = initiator_socket->get_direct_mem_ptr(trans, dmi_data);
        trans.set_address(orig_addr);
        if (ret) {
            sc_dt::uint64 start_addr = dmi_data.get_start_address();
            sc_dt::uint64 end_addr = dmi_data.get_end_address();
            unsigned char* dmi_ptr = dmi_data.get_dmi_ptr();

            if (mapped_addr < start_addr || mapped_addr > end_addr) {
                SCP_DEBUG(()) << "DMI grant did not contain requested mapped address 0x" << std::hex << mapped_addr;
                return false;
            }

            ret = map_window(start_addr, end_addr, &dmi_ptr);
            if (!ret) {
                SCP_DEBUG(()) << "DMI grant did not overlap mapped range";
                return false;
            }
            dmi_data.set_dmi_ptr(dmi_ptr);
            dmi_data.set_start_address(start_addr);
            dmi_data.set_end_address(end_addr);
            SCP_DEBUG(()) << "DMI was granted to range 0x" << std::hex << start_addr << " - 0x" << std::hex << end_addr;
        }
        return ret;
    }

    void invalidate_direct_mem_ptr(sc_dt::uint64 start, sc_dt::uint64 end)
    {
        sc_dt::uint64 start_addr = start;
        sc_dt::uint64 end_addr = end;
        if (!map_window(start_addr, end_addr)) {
            SCP_DEBUG(()) << "Ignoring invalidate_direct_mem_ptr outside mapped range 0x" << std::hex << start
                          << " - 0x" << std::hex << end;
            return;
        }
        SCP_DEBUG(()) << "invalidate_direct_mem_ptr request to range 0x" << std::hex << start_addr << " - 0x"
                      << std::hex << end_addr;
        target_socket->invalidate_direct_mem_ptr(start_addr, end_addr);
    }

private:
    cci::cci_broker_handle m_broker;
    uint64_t m_base_addr;
    uint64_t m_mapping_size;

public:
    tlm_utils::simple_target_socket<addrtr, DEFAULT_TLM_BUSWIDTH> target_socket;
    tlm_utils::simple_initiator_socket<addrtr, DEFAULT_TLM_BUSWIDTH> initiator_socket;
    cci::cci_param<uint64_t> p_mapped_base_addr;

public:
    explicit addrtr(const sc_core::sc_module_name& nm)
        : sc_core::sc_module(nm)
        , m_broker(cci::cci_get_broker())
        , initiator_socket("initiator_socket")
        , target_socket("target_socket")
        , p_mapped_base_addr("mapped_base_addr", 0, "base adress for mapping")
    {
        SCP_TRACE(())("addrtr constructor");
        target_socket.register_b_transport(this, &addrtr::b_transport);
        target_socket.register_transport_dbg(this, &addrtr::transport_dbg);
        target_socket.register_get_direct_mem_ptr(this, &addrtr::get_direct_mem_ptr);
        initiator_socket.register_invalidate_direct_mem_ptr(this, &addrtr::invalidate_direct_mem_ptr);
        m_base_addr = gs::cci_get<uint64_t>(m_broker,
                                            std::string(sc_core::sc_module::name()) + ".target_socket.address");
        m_mapping_size = gs::cci_get<uint64_t>(m_broker,
                                               std::string(sc_core::sc_module::name()) + ".target_socket.size");
    }

    addrtr() = delete;

    addrtr(const addrtr&) = delete;

    virtual ~addrtr() {}
};

extern "C" void module_register();

#endif
