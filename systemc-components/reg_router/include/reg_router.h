/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _GREENSOCS_BASE_COMPONENTS_REG_ROUTER_H
#define _GREENSOCS_BASE_COMPONENTS_REG_ROUTER_H

#include <cinttypes>
#include <iomanip>
#include <cci_configuration>
#include <systemc>
#include <tlm>
#include <scp/report.h>
#include <scp/helpers.h>
#include <tlm_utils/multi_passthrough_initiator_socket.h>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <cciutils.h>
#include <module_factory_registery.h>
#include <module_factory_container.h>
#include <tlm_sockets_buswidth.h>
#include <router_if.h>
#include <vector>
#include <memory>
#include <map>
#include <functional>

#define ENABLE_MODULE_NAME_DBG_INFO 1
namespace gs {

template <unsigned int BUSWIDTH = DEFAULT_TLM_BUSWIDTH>
class reg_router : public sc_core::sc_module, public gs::router_if<BUSWIDTH>
{
    SCP_LOGGER(());
    SCP_LOGGER((DMI), "dmi");

    using typename gs::router_if<BUSWIDTH>::target_info;
    using initiator_socket_type = typename gs::router_if<BUSWIDTH>::template multi_passthrough_initiator_socket_spying<
        reg_router<BUSWIDTH>>;
    using gs::router_if<BUSWIDTH>::bound_targets;

    void register_boundto(std::string s) override
    {
        s = gs::router_if<BUSWIDTH>::nameFromSocket(s);
        std::shared_ptr<target_info> ti_ptr = std::make_shared<target_info>(); // Create shared_ptr
        ti_ptr->name = s;
        ti_ptr->index = bound_targets.size();
        SCP_DEBUG(()) << "Connecting : " << ti_ptr->name;
        auto tmp = name();
        int i;
        for (i = 0; i < s.length(); i++)
            if (s[i] != tmp[i]) break;
        ti_ptr->shortname = s.substr(i);
        bound_targets.push_back(ti_ptr); // Add shared_ptr
    }

public:
    void b_transport(int id, tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        sc_dt::uint64 addr = trans.get_address();

        std::shared_ptr<target_info> ti = decode_address(trans); // Use shared_ptr

        if (!ti) {
            SCP_WARN(())("Attempt to access unknown location in register memory at offset 0x{:x}", addr);
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

#ifdef ENABLE_MODULE_NAME_DBG_INFO
        uint64_t mod_addr = 0;
        bool mod_found = false;
        const auto& mod_name = get_module_name(trans, mod_addr, mod_found);
        if (!mod_name.empty())
            SCP_DEBUG(()) << "b_transport: " << txn_command_str(trans) << " to Register at address: 0x" << std::hex
                          << trans.get_address() << " which belongs to Module: [" << mod_name << "]";
        if (m_pre_b_transport_callback) m_pre_b_transport_callback(mod_found, mod_addr);
#endif

        bool found_cb = do_callbacks(trans, delay);
        if (trans.get_response_status() >= tlm::TLM_INCOMPLETE_RESPONSE) {
            SCP_TRACEALL(()) << "call b_transport: " << txn_to_str(trans, false, found_cb);
            if (ti->use_offset) trans.set_address(addr - ti->address);
            initiator_socket[ti->index]->b_transport(trans, delay);
            if (ti->use_offset) trans.set_address(addr);
            SCP_TRACEALL(()) << "b_transport returned: " << txn_to_str(trans, false, found_cb);
        }
        if (trans.get_response_status() >= tlm::TLM_OK_RESPONSE) {
            do_callbacks(trans, delay);
        }
        if (cb_targets.size() > 0) {
            trans.set_dmi_allowed(false);
        }
    }

    unsigned int transport_dbg(int id, tlm::tlm_generic_payload& trans)
    {
        sc_dt::uint64 addr = trans.get_address();
        // transport_dbg transactions should only be handled by register memory
        std::shared_ptr<target_info> ti = decode_address(trans); // Use shared_ptr

        if (!ti) {
            SCP_WARN(())("transport_dbg: Attempt to access unknown location in register memory at offset 0x{:x}", addr);
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return 0;
        }

#ifdef ENABLE_MODULE_NAME_DBG_INFO
        uint64_t mod_addr = 0;
        bool mod_found = false;
        const auto& mod_name = get_module_name(trans, mod_addr, mod_found);
        if (!mod_name.empty())
            SCP_DEBUG(()) << "transport_dbg: " << txn_command_str(trans) << " to Register at address: 0x" << std::hex
                          << trans.get_address() << " which belongs to Module: [" << mod_name << "]";
        if (m_pre_b_transport_callback) m_pre_b_transport_callback(mod_found, mod_addr);
#endif

        if (ti->use_offset) trans.set_address(addr - ti->address);
        SCP_TRACEALL(()) << "[REG-MEM] call transport_dbg [txn]: " << scp::scp_txn_tostring(trans);
        unsigned int ret = initiator_socket[ti->index]->transport_dbg(trans);
        if (ti->use_offset) trans.set_address(addr);
        return ret;
    }

    bool get_direct_mem_ptr(int id, tlm::tlm_generic_payload& trans, tlm::tlm_dmi& dmi_data)
    {
        SCP_TRACE(()) << "[REG-MEM] call get_direct_mem_ptr (not allowed) [txn]: " << scp::scp_txn_tostring(trans);
        return false;
    }

    std::string txn_to_str(tlm::tlm_generic_payload& trans, bool is_callback = false, bool found_callback = false)
    {
        std::stringstream ss;
        std::string cmd = txn_command_str(trans);
        if (is_callback && found_callback) {
            if (cmd != "IGNORE" && cmd != "UNKNOWN") {
                if ((trans.get_response_status() == tlm::TLM_INCOMPLETE_RESPONSE))
                    ss << "[PRE-" << cmd << "] Register callbacks [txn]: ";
                else if ((trans.get_response_status() == tlm::TLM_OK_RESPONSE))
                    ss << "[POST-" << cmd << "] Register callbacks [txn]: ";
            }
        } else if (found_callback) {
            ss << "[REG-MEM] (" << cmd << " TO REG-MEM BEFORE " << cmd << " CB) [txn]: ";
        } else {
            ss << "[REG-MEM] (NO CB FOUND) [txn]: ";
        }

        ss << scp::scp_txn_tostring(trans);
        return ss.str();
    }

    std::string txn_command_str(const tlm::tlm_generic_payload& trans)
    {
        std::string cmd;
        switch (trans.get_command()) {
        case tlm::TLM_READ_COMMAND:
            cmd = "READ";
            break;
        case tlm::TLM_WRITE_COMMAND:
            cmd = "WRITE";
            break;
        case tlm::TLM_IGNORE_COMMAND:
            cmd = "IGNORE";
            break;
        default:
            cmd = "UNKNOWN";
            break;
        }
        return cmd;
    }

#ifdef ENABLE_MODULE_NAME_DBG_INFO
    const std::string& get_module_name(const tlm::tlm_generic_payload& trans, uint64_t& mod_addr, bool& mod_found)
    {
        static const std::string empty;
        if (mod_addr_name_map.empty()) {
            return empty;
        }
        uint64_t addr = trans.get_address();
        auto search = mod_addr_name_map.equal_range(addr);
        if (search.first != mod_addr_name_map.end() && search.first->first == addr) {
            if ((addr - search.first->first) < search.first->second.first) {
                mod_addr = search.first->first;
                mod_found = true;
                return search.first->second.second;
            }
        } else if (search.second != mod_addr_name_map.begin()) {
            auto expected_node = std::prev(search.second);
            if ((addr - expected_node->first) < expected_node->second.first) {
                mod_addr = expected_node->first;
                mod_found = true;
                return expected_node->second.second;
            } else {
                mod_found = false;
            }
        } else {
            mod_found = false;
        }
        return empty;
    }
#endif

    // Priority semantics match router.h: lower numeric value = higher priority
    // (0 is strongest, default when unset). Callbacks fan out to all peers at
    // the strongest priority; equal-priority mem targets are a fatal collision.
    bool do_callbacks(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        if (cb_targets.empty()) {
            return false;
        }

        sc_dt::uint64 addr = trans.get_address();

        std::vector<std::shared_ptr<target_info>> claimants;
        uint32_t best_prio = 0;
        for (auto& kv : cb_targets) {
            const auto& cand = kv.second;
            if (!cand->claims(addr)) continue;
            if (claimants.empty() || cand->priority < best_prio) {
                claimants.clear();
                claimants.push_back(cand);
                best_prio = cand->priority;
            } else if (cand->priority == best_prio) {
                claimants.push_back(cand);
            }
        }

        if (claimants.empty()) return false;

        for (const auto& ti : claimants) {
            SCP_TRACEALL(()) << "call b_transport (fan-out prio " << best_prio
                             << "): " << txn_to_str(trans, true, true);
            if (ti->use_offset) trans.set_address(addr - ti->address);
            initiator_socket[ti->index]->b_transport(trans, delay);
            if (ti->use_offset) trans.set_address(addr);
            SCP_TRACEALL(()) << "b_transport returned : " << txn_to_str(trans, true, true);
            if (trans.get_response_status() <= tlm::TLM_GENERIC_ERROR_RESPONSE) {
                SCP_WARN(()) << "Accesing register callback at addr: " << std::hex << addr << " (target " << ti->name
                             << ") returned with: " << trans.get_response_string();
                break;
            }
        }
        return true;
    }

    std::shared_ptr<target_info> decode_address(tlm::tlm_generic_payload& trans) override
    {
        lazy_initialize();

        sc_dt::uint64 addr = trans.get_address();

        std::shared_ptr<target_info> best;
        for (auto& ti : mem_targets) {
            if (!ti->claims(addr)) continue;
            if (!best || ti->priority < best->priority)
                best = ti;
            else if (ti->priority == best->priority && ti != best)
                SCP_FATAL(()) << "Two targets with equal priority both claim address 0x" << std::hex << addr;
        }
        return best;
    }

protected:
    virtual void before_end_of_elaboration() override
    {
        if (!lazy_init) lazy_initialize();
    }

protected:
    static std::vector<sc_dt::uint64> parse_u64_csv(const std::string& s)
    {
        std::vector<sc_dt::uint64> out;
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && (s[i] == ' ' || s[i] == ',')) ++i;
            if (i >= s.size()) break;
            size_t consumed = 0;
            sc_dt::uint64 v = std::stoull(s.substr(i), &consumed, 0);
            out.push_back(v);
            i += consumed;
        }
        return out;
    }

    // Walk a's element grid and return true iff any byte of an element is claimed by b.
    // O(prod(a.counts) * elem_size) at bind time; fine at elaboration.
    static bool banks_element_overlap(const std::shared_ptr<target_info>& a, const std::shared_ptr<target_info>& b)
    {
        const auto& s = a->dim_strides.empty() ? std::vector<sc_dt::uint64>{ a->stride } : a->dim_strides;
        const auto& c = a->dim_counts.empty() ? std::vector<sc_dt::uint64>{ 1 } : a->dim_counts;
        std::vector<size_t> idx(s.size(), 0);
        while (true) {
            sc_dt::uint64 off = 0;
            for (size_t k = 0; k < s.size(); k++) off += idx[k] * s[k];
            for (sc_dt::uint64 byte = 0; byte < a->elem_size; byte++) {
                if (b->claims(a->address + off + byte)) return true;
            }
            size_t k = s.size();
            while (k-- > 0) {
                if (++idx[k] < c[k]) break;
                idx[k] = 0;
                if (k == 0) return false;
            }
            if (k == (size_t)-1) return false;
        }
    }

    void lazy_initialize() override
    {
        if (initialized) return;
        initialized = true;

        for (auto& ti_ptr : bound_targets) { // Iterate over shared_ptrs
            std::string name = ti_ptr->name;
            std::string src;

            ti_ptr->address = gs::cci_get<uint64_t>(m_broker, name + ".address");
            ti_ptr->size = gs::cci_get<uint64_t>(m_broker, name + ".size");
            ti_ptr->use_offset = gs::cci_get_d<bool>(m_broker, name + ".relative_addresses", true);
            ti_ptr->is_callback = gs::cci_get_d<bool>(m_broker, name + ".is_callback", false);
            ti_ptr->priority = gs::cci_get_d<uint32_t>(m_broker, name + ".priority", 0);
            ti_ptr->stride = gs::cci_get_d<uint64_t>(m_broker, name + ".stride", 0);
            ti_ptr->elem_size = gs::cci_get_d<uint64_t>(m_broker, name + ".elem_size", 0);

            std::string strides_str = gs::cci_get_d<std::string>(m_broker, name + ".dim_strides", std::string());
            std::string counts_str = gs::cci_get_d<std::string>(m_broker, name + ".dim_counts", std::string());
            if (!strides_str.empty() && !counts_str.empty()) {
                ti_ptr->dim_strides = parse_u64_csv(strides_str);
                ti_ptr->dim_counts = parse_u64_csv(counts_str);
                if (ti_ptr->dim_strides.size() != ti_ptr->dim_counts.size())
                    SCP_FATAL(()) << name << " has mismatched dim_strides / dim_counts";
            }

            SCP_INFO(()) << "Address map " << ti_ptr->name + " at" << " address " << "0x" << std::hex << ti_ptr->address
                         << " size " << "0x" << std::hex << ti_ptr->size
                         << (ti_ptr->stride ? " stride 0x" + std::to_string(ti_ptr->stride) : "")
                         << (ti_ptr->dim_strides.empty() ? "" : (" dims=" + std::to_string(ti_ptr->dim_strides.size())))
                         << (ti_ptr->use_offset ? " (with relative address) " : "")
                         << (ti_ptr->is_callback ? " (callback) " : "");

            if (ti_ptr->is_callback) {
                cb_targets.insert(std::make_pair(ti_ptr->address, ti_ptr));
            } else {
                if (!mem_targets.empty()) {
                    for (const auto& mem : mem_targets) {
                        bool overlaps_range = ((ti_ptr->address >= mem->address) &&
                                               ((ti_ptr->address - mem->address) < mem->size)) ||
                                              ((ti_ptr->address < mem->address) &&
                                               ((ti_ptr->address + ti_ptr->size) > mem->address));
                        if (!overlaps_range) continue;

                        bool ti_gapped = ti_ptr->stride != 0 || !ti_ptr->dim_strides.empty();
                        bool mem_gapped = mem->stride != 0 || !mem->dim_strides.empty();
                        if (!ti_gapped || !mem_gapped) {
                            SCP_WARN(()) << ti_ptr->name << " overlaps with " << mem->name;
                        } else if (banks_element_overlap(ti_ptr, mem)) {
                            SCP_WARN(()) << ti_ptr->name << " element footprint overlaps with " << mem->name;
                        }
                    }
                }
                mem_targets.push_back(ti_ptr);
            }
        }
    }

public:
    explicit reg_router(
        const sc_core::sc_module_name& nm,
        const std::map<uint64_t, std::pair<uint64_t, std::string>>& p_mod_addr_name_map =
            std::map<uint64_t, std::pair<uint64_t, std::string>>(),
        const std::function<void(bool, uint64_t)>& pre_b_transport_callback = std::function<void(bool, uint64_t)>(),
        cci::cci_broker_handle broker = cci::cci_get_broker())
        : sc_core::sc_module(nm)
        , initiator_socket("initiator_socket", [&](std::string s) -> void { register_boundto(s); })
        , target_socket("target_socket")
        , m_broker(broker)
        , lazy_init("lazy_init", false, "Initialize the reg_router lazily (eg. during simulation rather than BEOL)")
        , mod_addr_name_map(p_mod_addr_name_map)
        , m_pre_b_transport_callback(pre_b_transport_callback)
    {
        SCP_DEBUG(()) << "reg_router constructed";

        target_socket.register_b_transport(this, &reg_router::b_transport);
        target_socket.register_transport_dbg(this, &reg_router::transport_dbg);
        target_socket.register_get_direct_mem_ptr(this, &reg_router::get_direct_mem_ptr);
        // memory model doesn't implement invalidate_direct_mem_ptr so no need to register it
        SCP_DEBUG((DMI)) << "reg_router Initializing DMI SCP reporting";
    }

    reg_router() = delete;

    reg_router(const reg_router&) = delete;

    ~reg_router() = default;

public:
    // make the sockets public for binding
    initiator_socket_type initiator_socket;
    tlm_utils::multi_passthrough_target_socket<reg_router<BUSWIDTH>, BUSWIDTH> target_socket;
    cci::cci_broker_handle m_broker;
    cci::cci_param<bool> lazy_init;

private:
    std::vector<std::shared_ptr<target_info>> mem_targets;
    std::multimap<sc_dt::uint64, std::shared_ptr<target_info>> cb_targets;
    std::map<uint64_t, std::pair<uint64_t, std::string>> mod_addr_name_map;
    std::function<void(bool, uint64_t)> m_pre_b_transport_callback;
    bool initialized = false;
};
} // namespace gs

extern "C" void module_register();
#endif
