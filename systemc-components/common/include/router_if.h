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
