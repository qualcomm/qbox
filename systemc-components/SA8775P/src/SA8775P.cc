/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#define gen_xstr(s) _gen_str(s)
#define _gen_str(s) #s

#include "SA8775P.h"
#include <zip.h>

namespace quic {

SA8775P::SA8775P(sc_core::sc_module_name _name, sc_core::sc_object* router, sc_core::sc_object* reg_memory)
    : sc_core::sc_module(_name)
    , is_shared_memories("shared_platform_memories", false,
                         "enable/disable the shared_memory property of the generated memories (default false)")
    , p_zipfile("zipfile", "", "path of the generated platform zip archive with memory and module names information")
    , m_broker(cci::cci_get_broker())
    , m_archive(zip_open(p_zipfile.get_value().c_str(), ZIP_RDONLY, nullptr))
    , m_router(dynamic_cast<gs::router<>*>(router))
    , m_reg_memory(dynamic_cast<gs::gs_memory<>*>(reg_memory))
    , reset_mem("reset_mem")
    , reset_in("reset")
{
    SCP_TRACE(())("Constructor");
    if (!m_router) {
        SCP_FATAL(()) << "router argument to SA8775P constructor is NULL!";
    }
    if (!m_reg_memory) {
        SCP_FATAL(()) << "reg_memory argument to SA8775P constructor is NULL!";
    }
    auto mem_it = m_mem_addr_name_map.begin();
    std::string platform_name = std::string(this->name());
    if (!m_mem_addr_name_map.empty()) {
        memories.init(
            m_mem_addr_name_map.size(),
            [this, &mem_it, &platform_name](const char* name, size_t n) -> gs::gs_memory<>* {
                std::string mem_addr = platform_name + "." + std::string(mem_it->second.second) +
                                       ".target_socket.address";
                if (!m_broker.has_preset_value(mem_addr))
                    m_broker.set_preset_cci_value(mem_addr, cci::cci_value(mem_it->first));
                std::string mem_size = platform_name + "." + std::string(mem_it->second.second) + ".target_socket.size";
                if (!m_broker.has_preset_value(mem_size))
                    m_broker.set_preset_cci_value(mem_size, cci::cci_value(mem_it->second.first));
                std::string mem_rel_addr = platform_name + "." + std::string(mem_it->second.second) +
                                           ".target_socket.relative_addresses";
                if (!m_broker.has_preset_value(mem_rel_addr))
                    m_broker.set_preset_cci_value(mem_rel_addr, cci::cci_value(true));
                std::string mem_priority = platform_name + "." + std::string(mem_it->second.second) +
                                           ".target_socket.priority";
                if (!m_broker.has_preset_value(mem_priority))
                    m_broker.set_preset_cci_value(mem_priority, cci::cci_value(1));
                std::string mem_shared = platform_name + "." + std::string(mem_it->second.second) + ".shared_memory";
                if (!m_broker.has_preset_value(mem_shared))
                    m_broker.set_preset_cci_value(mem_shared, cci::cci_value(is_shared_memories.get_value()));
                auto new_mem = new gs::gs_memory<>(mem_it++->second.second.c_str());
                return new_mem;
            });
    } else {
        SCP_FATAL(()) << "Platform memories container is empty, may be memories are not generated successfully for "
                         "SA8775P platform!";
    }
    for (auto& m : memories) {
        m_router->initiator_socket(m.socket);
        reset_mem.bind(m.reset);
    }
    m_broker.set_preset_cci_value(platform_name + ".reg_router.target_socket.address", cci::cci_value(0));
    m_broker.set_preset_cci_value(
        platform_name + ".reg_router.target_socket.size",
        m_broker.get_preset_cci_value(std::string(m_reg_memory->name()) + ".target_socket.size"));
    m_broker.set_preset_cci_value(platform_name + ".reg_router.target_socket.priority", cci::cci_value(2));
    m_broker.set_preset_cci_value(platform_name + ".reg_router.target_socket.relative_addresses", cci::cci_value(true));
    m_broker.set_preset_cci_value(platform_name + ".reg_router.target_socket.lazy_init", cci::cci_value(true));
    m_reg_router = std::make_unique<gs::reg_router<>>(
        "reg_router", m_mod_addr_name_map, [this](bool mod_found, uint64_t mod_addr) -> void {
            if (!mod_found || (loaded_mods.find(mod_addr) != loaded_mods.end())) {
                return;
            }
            std::stringstream addr_str;
            addr_str << "0x" << std::hex << mod_addr << ".bin";
            std::string mod_file_name(addr_str.str());
            addr_str << ".zip";
            std::string compressed_mod_file_name(addr_str.str());
            m_reg_memory->load.zip_file_load(m_archive, p_zipfile.get_value(), mod_addr, compressed_mod_file_name, 0,
                                             m_mod_addr_name_map[mod_addr].first, true, mod_file_name);
            loaded_mods[mod_addr] = true;
        });
    m_reg_router->initiator_socket(m_reg_memory->socket);
    m_router->initiator_socket(m_reg_router->target_socket);
    reset_in.register_value_changed_cb([&](bool value) { do_reset(value); });
}
void SA8775P::do_reset(bool value)
{
    if (value) {
        SCP_WARN(()) << "Platform Reset";
        if (loaded_mods.size() > 0) loaded_mods.clear();
    }
    for (int i = 0; i < reset_mem.size(); i++) {
        reset_mem[i]->write(value);
    }
}

} // namespace quic

typedef quic::SA8775P SA8775P;

void module_register() { GSC_MODULE_REGISTER_C(SA8775P, sc_core::sc_object*, sc_core::sc_object*); }
