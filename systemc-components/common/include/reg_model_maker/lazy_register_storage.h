/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef GS_LAZY_REGISTER_STORAGE_H
#define GS_LAZY_REGISTER_STORAGE_H

#include <reg_router.h>
#include <ports/target-signal-socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <algorithm>
#include <functional>
#include <limits>
#include <map>

namespace gs {

// Separate initialization probes from storage traffic. Model callbacks never
// run on this path. Addresses on target_socket are platform absolute addresses.
// Image regions are cached automatically until the existing model reset is
// forwarded by reg_model_maker. Reset invalidates both this cache and upstream
// DMI; it does not clear the platform loader's own image bookkeeping.
class lazy_register_storage : public sc_core::sc_module
{
public:
    tlm_utils::multi_passthrough_target_socket<lazy_register_storage, DEFAULT_TLM_BUSWIDTH> target_socket;
    tlm_utils::simple_initiator_socket_optional<lazy_register_storage, DEFAULT_TLM_BUSWIDTH> image_socket;
    tlm_utils::simple_initiator_socket<lazy_register_storage, DEFAULT_TLM_BUSWIDTH> memory_socket;
    TargetSignalSocket<bool> reset;

    lazy_register_storage(sc_core::sc_module_name name, std::function<reg_router<>*()> resolve_router,
                          uint64_t memory_offset = 0)
        : sc_core::sc_module(name)
        , target_socket("target_socket")
        , image_socket("image_socket")
        , memory_socket("memory_socket")
        , reset("reset")
        , m_resolve_router(std::move(resolve_router))
        , m_memory_offset(memory_offset)
    {
        target_socket.register_b_transport(this, &lazy_register_storage::b_transport);
        target_socket.register_transport_dbg(this, &lazy_register_storage::transport_dbg);
        target_socket.register_get_direct_mem_ptr(this, &lazy_register_storage::get_direct_mem_ptr);
        memory_socket.register_invalidate_direct_mem_ptr(this, &lazy_register_storage::memory_invalidated);
        image_socket.register_invalidate_direct_mem_ptr(this, &lazy_register_storage::images_invalidated);
        reset.register_value_changed_cb([this](bool asserted) {
            m_in_reset = asserted;
            // Clear on both edges: accesses during reset must not seed a cache
            // that survives another recipient's image/memory reset callback.
            invalidate();
        });
    }

    void invalidate()
    {
        m_loaded.clear();
        if (sc_core::sc_get_status() < sc_core::SC_END_OF_ELABORATION) return;
        for (unsigned i = 0; i < target_socket.size(); ++i)
            target_socket[i]->invalidate_direct_mem_ptr(0, std::numeric_limits<uint64_t>::max());
    }

protected:
    void before_end_of_elaboration() override
    {
        m_image_router = m_resolve_router ? m_resolve_router() : nullptr;
        if (m_image_router) image_socket.bind(m_image_router->target_socket);
    }

private:
    std::function<reg_router<>*()> m_resolve_router;
    reg_router<>* m_image_router = nullptr;
    uint64_t m_memory_offset;
    bool m_in_reset = false;
    std::map<uint64_t, uint64_t> m_loaded;

    bool can_cache() const { return !m_in_reset; }

    static uint64_t access_length(const tlm::tlm_generic_payload& txn)
    {
        const auto length = txn.get_data_length();
        const auto width = txn.get_streaming_width();
        return width ? std::min(length, width) : length;
    }

    bool ensure_loaded(uint64_t address, uint64_t length, bool require_regions = false)
    {
        if (address < m_memory_offset) return false;
        if (!length) return true;
        if (length - 1 > std::numeric_limits<uint64_t>::max() - address) return false;
        if (!m_image_router) return true;

        while (length) {
            uint64_t base = 0, size = 0;
            const bool found = m_image_router->get_module_region(address, base, size);
            // Unknown/gap bytes retain raw-memory behavior, but must not grant
            // DMI across an unknown range that could contain unloaded images.
            if (!found && require_regions) return false;
            const uint64_t step = found ? std::min(length, size - (address - base)) : 1;
            if (!found || !can_cache() || !m_loaded.count(base)) {
                unsigned char byte = 0;
                tlm::tlm_generic_payload probe;
                probe.set_command(tlm::TLM_READ_COMMAND);
                probe.set_address(address);
                probe.set_data_ptr(&byte);
                probe.set_data_length(1);
                probe.set_streaming_width(1);
                probe.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
                if (image_socket->transport_dbg(probe) != 1 || !probe.is_response_ok()) return false;
                if (found && can_cache()) m_loaded[base] = size;
            }
            length -= step;
            if (length) address += step;
        }
        return true;
    }

    void b_transport(int, tlm::tlm_generic_payload& txn, sc_core::sc_time& delay)
    {
        const auto address = txn.get_address();
        if (!ensure_loaded(address, access_length(txn))) {
            txn.set_dmi_allowed(false);
            txn.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        txn.set_address(address - m_memory_offset);
        memory_socket->b_transport(txn, delay);
        txn.set_address(address);
        if (m_image_router && !can_cache()) txn.set_dmi_allowed(false);
    }

    unsigned transport_dbg(int, tlm::tlm_generic_payload& txn)
    {
        const auto address = txn.get_address();
        if (!ensure_loaded(address, access_length(txn))) {
            txn.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return 0;
        }
        txn.set_address(address - m_memory_offset);
        const auto count = memory_socket->transport_dbg(txn);
        txn.set_address(address);
        if (m_image_router && !can_cache()) txn.set_dmi_allowed(false);
        return count;
    }

    bool get_direct_mem_ptr(int, tlm::tlm_generic_payload& txn, tlm::tlm_dmi& dmi)
    {
        if (m_image_router && !can_cache()) return false;
        const uint64_t address = txn.get_address();
        // gs_register requests its whole bank span here, including stride gaps.
        const uint64_t length = std::max(1u, txn.get_data_length());
        if (length - 1 > std::numeric_limits<uint64_t>::max() - address) return false;
        // Check coverage before loading: a bank with unmapped stride gaps is
        // serviced normally, without partially preloading a failed DMI request.
        if (m_image_router) {
            uint64_t cursor = address, remaining = length;
            while (remaining) {
                uint64_t base = 0, size = 0;
                if (!m_image_router->get_module_region(cursor, base, size)) return false;
                const auto step = std::min(remaining, size - (cursor - base));
                remaining -= step;
                if (remaining) cursor += step;
            }
        }
        if (!ensure_loaded(address, length, true)) return false;
        txn.set_address(address - m_memory_offset);
        const bool granted = memory_socket->get_direct_mem_ptr(txn, dmi);
        txn.set_address(address);
        if (!granted) return false;
        if (dmi.get_end_address() > std::numeric_limits<uint64_t>::max() - m_memory_offset) return false;
        const uint64_t start = dmi.get_start_address() + m_memory_offset;
        const uint64_t end = dmi.get_end_address() + m_memory_offset;
        const uint64_t last = address + length - 1;
        if (start > address || end < last) return false;
        // Never expose the rest of a memory allocation: only this initialized span.
        dmi.set_dmi_ptr(dmi.get_dmi_ptr() + (address - start));
        dmi.set_start_address(address);
        dmi.set_end_address(last);
        return true;
    }

    void memory_invalidated(sc_dt::uint64, sc_dt::uint64) { invalidate(); }
    void images_invalidated(sc_dt::uint64, sc_dt::uint64) { invalidate(); }
};

} // namespace gs
#endif
