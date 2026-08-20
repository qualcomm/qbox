/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _GREENSOCS_BASE_COMPONENTS_ROUTER_IF_H
#define _GREENSOCS_BASE_COMPONENTS_ROUTER_IF_H

#include <systemc>
#include <tlm>
#include <scp/report.h>
#include <scp/helpers.h>
#include <tlm_utils/multi_passthrough_initiator_socket.h>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_sockets_buswidth.h>
#include <string>
#include <memory>
#include <vector>

namespace gs {
template <unsigned int BUSWIDTH = DEFAULT_TLM_BUSWIDTH>
class router_if
{
protected:
    template <typename MOD>
    class multi_passthrough_initiator_socket_spying
        : public tlm_utils::multi_passthrough_initiator_socket<MOD, BUSWIDTH>
    {
        using typename tlm_utils::multi_passthrough_initiator_socket<MOD, BUSWIDTH>::base_target_socket_type;

        const std::function<void(std::string)> register_cb;

    public:
        multi_passthrough_initiator_socket_spying(const char* name, const std::function<void(std::string)>& f)
            : tlm_utils::multi_passthrough_initiator_socket<MOD, BUSWIDTH>::multi_passthrough_initiator_socket(name)
            , register_cb(f)
        {
        }

        void bind(base_target_socket_type& socket)
        {
            tlm_utils::multi_passthrough_initiator_socket<MOD, BUSWIDTH>::bind(socket);
            register_cb(socket.get_base_export().name());
        }
    };

public:
    struct target_info {
        size_t index;
        std::string name;
        sc_dt::uint64 address;
        sc_dt::uint64 size;
        uint32_t priority;
        bool use_offset;
        bool is_callback;
        bool chained;
        std::string shortname;
        // 1D bank fields (used when dim_strides is empty).
        // stride == 0 means plain range (scalar / contiguous register).
        sc_dt::uint64 stride = 0;
        sc_dt::uint64 elem_size = 0;
        // N-D bank layout, outer dim first. Non-empty overrides the 1D fields.
        std::vector<sc_dt::uint64> dim_strides;
        std::vector<sc_dt::uint64> dim_counts;

        bool claims(sc_dt::uint64 addr) const
        {
            if (addr < address || (addr - address) >= size) return false;
            if (!dim_strides.empty()) {
                sc_dt::uint64 r = addr - address;
                for (size_t k = 0; k < dim_strides.size(); k++) {
                    sc_dt::uint64 idx = r / dim_strides[k];
                    if (idx >= dim_counts[k]) return false;
                    r = r % dim_strides[k];
                }
                return r < elem_size;
            }
            if (stride == 0) return true;
            return ((addr - address) % stride) < elem_size;
        }
    };

    void rename_last(std::string s)
    {
        auto ti = bound_targets.back();
        ti->name = s;
    }

    std::vector<std::shared_ptr<target_info>> get_bound_targets() { return bound_targets; } // Returns shared_ptrs

    virtual ~router_if() = default;

protected:
    std::string parent(std::string name) { return name.substr(0, name.find_last_of('.')); }

    /* NB use the EXPORT name, so as not to be hassled by the _port_0*/
    std::string nameFromSocket(std::string s) { return s; }

    virtual void register_boundto(std::string s) = 0;

    virtual std::shared_ptr<target_info> decode_address(tlm::tlm_generic_payload& trans) = 0; // Returns shared_ptr

    virtual void lazy_initialize() = 0;

    std::vector<std::shared_ptr<target_info>> bound_targets; // Changed to shared_ptr
};
} // namespace gs

#endif
