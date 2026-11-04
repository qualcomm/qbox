/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <cci_configuration>
#include <gs_memory.h>
#include <reg_router.h>
#include <registers.h>
#include <ports/initiator-signal-socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/multi_passthrough_target_socket.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs {

class register_model : public sc_core::sc_module
{
    class register_bindings
    {
    public:
        register_bindings(reg_router<>& router, gs_memory<>& memory)
            : m_router(router), m_memory(memory), m_broker(cci::cci_get_broker())
        {
            m_router.initiator_socket.bind(m_memory.socket);
        }

        template <typename T>
        void bind_at(gs_register<T>& reg, std::uint64_t base, int priority = 0)
        {
            const auto name = target_socket_name(reg);
            const auto offset = reg.get_offset();
            const auto span = reg.get_size();
            const auto maximum = std::numeric_limits<std::uint64_t>::max();
            if (span == 0 || offset > maximum - base || span - 1 > maximum - (base + offset)) {
                throw std::invalid_argument(name + ": register address range overflows address space");
            }

            reg.p_offset = base + offset;

            // use register address policy: callbacks use relative offsets by default
            if constexpr (is_gs_bank<T>::value) {
                set(name + ".dim_strides", encode(reg.get_dim_strides()));
                set(name + ".dim_counts", encode(reg.get_dim_counts()));
            }

            if (priority != 0) set(name + ".priority", priority);

            reg.initiator_socket.bind(m_memory.socket);
            m_router.initiator_socket.bind(reg);
            m_router.rename_last(name);
        }

    private:
        template <typename T>
        void set(const std::string& name, T value)
        {
            auto parameter = m_broker.get_param_handle(name);
            if (parameter.is_valid()) {
                parameter.set_cci_value(cci::cci_value(value));
            } else if (!m_broker.has_preset_value(name)) {
                m_broker.set_preset_cci_value(name, cci::cci_value(value));
            } else {
                throw std::invalid_argument("register model cannot reconfigure " + name);
            }
        }

        template <typename T>
        static std::string target_socket_name(const gs_register<T>& reg)
        {
            std::string name(reg.p_offset.name());
            constexpr std::string_view suffix = ".address";
            if (name.size() < suffix.size() || std::string_view(name).substr(name.size() - suffix.size()) != suffix) {
                throw std::invalid_argument("invalid register address parameter name");
            }
            name.resize(name.size() - suffix.size());
            return name;
        }

        static std::string encode(const std::vector<std::uint64_t>& values)
        {
            std::string result;
            for (std::size_t index = 0; index < values.size(); ++index) {
                if (index != 0) {
                    result += ',';
                }
                result += std::to_string(values[index]);
            }
            return result;
        }

        reg_router<>& m_router;
        gs_memory<>& m_memory;
        cci::cci_broker_handle m_broker;
    };

public:
    using target_socket_type = tlm_utils::multi_passthrough_target_socket_optional<register_model,
                                                                                   DEFAULT_TLM_BUSWIDTH>;
    using module_address_map = std::map<std::uint64_t, std::pair<std::uint64_t, std::string>>;
    using access_callback = std::function<void(bool, std::uint64_t)>;

    class target_socket_bridge
        : public tlm_utils::multi_passthrough_target_socket<target_socket_bridge, DEFAULT_TLM_BUSWIDTH>
    {
    public:
        explicit target_socket_bridge(const char* name)
            : tlm_utils::multi_passthrough_target_socket<target_socket_bridge, DEFAULT_TLM_BUSWIDTH>(name)
            , m_access((std::string(name) + "_access").c_str())
        {
            this->register_b_transport(this, &target_socket_bridge::b_transport);
            this->register_transport_dbg(this, &target_socket_bridge::transport_dbg);
            this->register_get_direct_mem_ptr(this, &target_socket_bridge::get_direct_mem_ptr);
            m_access.register_invalidate_direct_mem_ptr(this, &target_socket_bridge::invalidate_direct_mem_ptr);
        }

        template <typename Socket>
        void forward_to(Socket& socket)
        {
            m_access.bind(socket);
        }

    private:
        void b_transport(int, tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
        {
            m_access->b_transport(trans, delay);
        }

        unsigned int transport_dbg(int, tlm::tlm_generic_payload& trans) { return m_access->transport_dbg(trans); }

        bool get_direct_mem_ptr(int, tlm::tlm_generic_payload& trans, tlm::tlm_dmi& dmi)
        {
            return m_access->get_direct_mem_ptr(trans, dmi);
        }

        void invalidate_direct_mem_ptr(sc_dt::uint64 start, sc_dt::uint64 end)
        {
            for (unsigned int index = 0; index < this->size(); ++index)
                (*this)[index]->invalidate_direct_mem_ptr(start, end);
        }

        tlm_utils::simple_initiator_socket<target_socket_bridge> m_access;
    };

    register_model(sc_core::sc_module_name name, gs_memory<>& memory, std::uint64_t base, std::uint64_t size,
                   bool relative_addresses = true, const module_address_map& modules = {},
                   const access_callback& before_access = {}, int priority = 0, bool lazy_init = false,
                   std::optional<std::uint64_t> mapped_address = {})
        : sc_core::sc_module(name)
        , m_target("target_socket")
        , m_router("router", modules, before_access)
        , m_access("access")
        , m_reset("reset")
        , m_base(configure_frontend(m_target.name(), base, size, relative_addresses, priority,
                                    mapped_address.value_or(base)))
        , m_size(size)
        , m_mapped_address(mapped_address.value_or(base))
        , m_relative_addresses(relative_addresses)
        , m_bindings(m_router, memory)
    {
        m_target.register_b_transport(this, &register_model::socket_b_transport);
        m_target.register_transport_dbg(this, &register_model::socket_transport_dbg);
        m_target.register_get_direct_mem_ptr(this, &register_model::socket_get_direct_mem_ptr);

        m_access.bind(m_router.target_socket);
        m_reset.bind(memory.reset);
        if (lazy_init) m_router.lazy_init = true;
    }

    target_socket_type& target_socket() { return m_target; }

    void reset(bool asserted) { m_reset->write(asserted); }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        trans.set_dmi_allowed(false);
        if (!valid_address(trans)) return;
        address_guard guard{ trans, trans.get_address() };
        trans.set_address(m_base +
                          (m_relative_addresses ? trans.get_address() : trans.get_address() - m_mapped_address));
        m_access->b_transport(trans, delay);
        trans.set_dmi_allowed(false);
    }

    unsigned int transport_dbg(tlm::tlm_generic_payload& trans)
    {
        if (!valid_address(trans)) return 0;
        address_guard guard{ trans, trans.get_address() };
        trans.set_address(m_base +
                          (m_relative_addresses ? trans.get_address() : trans.get_address() - m_mapped_address));
        return m_access->transport_dbg(trans);
    }

    bool get_direct_mem_ptr(tlm::tlm_generic_payload&, tlm::tlm_dmi&) { return false; }

    template <typename T>
    void bind_register(gs_register<T>& reg, int priority = 0)
    {
        bind_register_at(reg, m_base, priority);
    }

    template <typename T>
    void bind_register_at(gs_register<T>& reg, std::uint64_t base, int priority = 0)
    {
        const auto offset = reg.get_offset();
        const auto span = reg.get_size();
        if (base < m_base || base - m_base >= m_size || span == 0 || offset >= m_size - (base - m_base) ||
            span > m_size - (base - m_base) - offset) {
            throw std::invalid_argument(std::string(reg.p_offset.name()) + ": register lies outside model aperture");
        }
        m_bindings.bind_at(reg, base, priority);
    }

    class binding_group
    {
    public:
        explicit binding_group(register_model& model): m_model(model) {}

        template <typename T>
        void bind_at(gs_register<T>& reg, std::uint64_t base, int priority = 0)
        {
            m_model.bind_register_at(reg, base, priority);
            ++m_count;
        }

        std::size_t size() const { return m_count; }

    private:
        register_model& m_model;
        std::size_t m_count = 0;
    };

private:
    static std::uint64_t configure_frontend(const std::string& socket, std::uint64_t base, std::uint64_t size,
                                            bool relative_addresses, int priority, std::uint64_t mapped_address)
    {
        if (size && (base > std::numeric_limits<std::uint64_t>::max() - (size - 1) ||
                     mapped_address > std::numeric_limits<std::uint64_t>::max() - (size - 1))) {
            throw std::invalid_argument("register model aperture overflows address space");
        }

        auto broker = cci::cci_get_broker();
        std::vector<std::pair<std::string, cci::cci_value>> metadata{
            { socket + ".address", cci::cci_value(mapped_address) },
            { socket + ".size", cci::cci_value(size) },
            { socket + ".relative_addresses", cci::cci_value(relative_addresses) },
        };
        if (priority != 0) metadata.emplace_back(socket + ".priority", cci::cci_value(priority));

        for (const auto& entry : metadata) {
            if (broker.has_preset_value(entry.first) && !(broker.get_preset_cci_value(entry.first) == entry.second)) {
                throw std::invalid_argument(entry.first + ": conflicts with register model metadata");
            }
        }

        for (const auto& entry : metadata) {
            if (!broker.has_preset_value(entry.first)) {
                broker.set_preset_cci_value(entry.first, entry.second);
            }
            broker.lock_preset_value(entry.first);
        }
        return base;
    }

    struct address_guard {
        tlm::tlm_generic_payload& trans;
        const std::uint64_t address;
        ~address_guard() { trans.set_address(address); }
    };

    bool valid_address(tlm::tlm_generic_payload& trans) const
    {
        const auto address = trans.get_address();
        if (!m_relative_addresses && address < m_mapped_address) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }
        const auto offset = m_relative_addresses ? address : address - m_mapped_address;
        if (offset >= m_size || trans.get_data_length() > m_size - offset) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }
        return true;
    }

    void socket_b_transport(int, tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        b_transport(trans, delay);
    }

    unsigned int socket_transport_dbg(int, tlm::tlm_generic_payload& trans) { return transport_dbg(trans); }

    bool socket_get_direct_mem_ptr(int, tlm::tlm_generic_payload& trans, tlm::tlm_dmi& dmi)
    {
        return get_direct_mem_ptr(trans, dmi);
    }

    target_socket_type m_target;
    reg_router<> m_router;
    tlm_utils::simple_initiator_socket<register_model> m_access;
    InitiatorSignalSocket<bool> m_reset;
    const std::uint64_t m_base;
    const std::uint64_t m_size;
    const std::uint64_t m_mapped_address;
    const bool m_relative_addresses;
    register_bindings m_bindings;
};

} // namespace gs
