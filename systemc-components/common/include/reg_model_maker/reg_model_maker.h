/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _REG_MODEL_MAKER_H
#define _REG_MODEL_MAKER_H

#include <systemc>
#include <cci_configuration>
#include <scp/report.h>
#include <rapidjson/include/rapidjson/document.h>
#include <zip.h>
#include <cciutils.h>
#include <gs_memory.h>
#include <router.h>
#include <reg_router.h>
#include <registers.h>
#include <tlm_sockets_buswidth.h>
#include <ports/initiator-signal-socket.h>
#include <memory>
#include <vector>
#include <limits>
#include <map>
#include <list>
#include <algorithm>
namespace gs {

class json_zip_archive
{
    SCP_LOGGER((), "json_zip_archive");

    std::set<std::string> m_loaded;
    std::vector<uint8_t> m_bin_file_data;
    zip_t* m_archive;
    bool m_base_address_captured;
    uint64_t m_base_address;
    uint64_t m_parent_model_base_addr;
    bool m_is_platform_top;
    bool m_is_platform_model;
    uint64_t m_max_size;
    std::shared_ptr<gs::router<>> m_router;
    std::shared_ptr<gs::gs_memory<>> m_memory;
    std::string m_archive_name;

public:
    json_zip_archive(zip_t* archive)
        : m_archive(archive)
        , m_base_address_captured(false)
        , m_base_address(0)
        , m_parent_model_base_addr(0)
        , m_is_platform_top(false)
        , m_is_platform_model(false)
        , m_max_size(0)
        , m_router(nullptr)
        , m_memory(nullptr)
    {
        SCP_LOGGER_NAME().features[0] = "json_zip_archive";
        if (m_archive == nullptr) {
            SCP_FATAL(())("Can't open zip archive");
        }
        std::string json_top_file_name = get_json_topname();
        m_archive_name = json_top_file_name + ".zip";
        SCP_DEBUG(())("Loaded zip archive: {}", m_archive_name);
    }

    zip_t* get_zip_archive_ptr() { return m_archive; }

    std::string get_archive_name() { return m_archive_name; }

    uint64_t getNumber(rapidjson::Value& v)
    {
        if (v.IsNumber()) {
            return v.GetUint64();
        }
        if (v.IsString()) {
            const char* p = v.GetString();
            return std::strtoull(p, nullptr, 0);
        }
        SCP_FATAL(())("JSON Number neither number, nor string!");
        return 0;
    }

    std::vector<std::string> get_file_names_in_zip_archive()
    {
        std::vector<std::string> ret;
        zip_stat_t stat;
        zip_int64_t num_of_entries = zip_get_num_entries(m_archive, 0);
        if (num_of_entries < 0) SCP_FATAL(())("Can't get number of entries in the zip archive");
        for (zip_int64_t i = 0; i < num_of_entries; i++) {
            if (zip_stat_index(m_archive, i, ZIP_FL_NOCASE, &stat) < 0) {
                SCP_FATAL(())
                ("Can't execute zip_stat() on function get_file_names_in_zip_archive()");
            }
            ret.push_back(stat.name);
        }
        return ret;
    }

    std::string get_json_topname()
    {
        auto zip_arch_file_names = get_file_names_in_zip_archive();
        std::string ret = "";
        for (const auto& file_name : zip_arch_file_names) {
            if (file_name.find(".json", file_name.length() - std::string(".json").length()) != file_name.npos) {
                ret = file_name.substr(0, file_name.length() - std::string(".json").length());
                break;
            }
        }
        if (ret.empty()) SCP_FATAL(())
        ("Can't find any json config files in the zip archive");
        return ret;
    }

    std::string get_bin_file_name()
    {
        auto zip_arch_file_names = get_file_names_in_zip_archive();
        std::string ret = "";
        for (const auto& file_name : zip_arch_file_names) {
            if (file_name.find(".bin", file_name.length() - std::string(".bin").length()) != file_name.npos) {
                ret = file_name;
                break;
            }
        }
        if (ret.empty()) SCP_FATAL(())
        ("Can't find any register binary reset values files in the zip archive: {}", get_archive_name());
        return ret;
    }

    uint64_t get_bin_file_size()
    {
        zip_stat_t z_stat;
        auto zip_arch_file_names = get_file_names_in_zip_archive();
        std::string bin_file_name;
        for (const auto& file_name : zip_arch_file_names) {
            if (file_name.find(".bin", file_name.length() - std::string(".bin").length()) != file_name.npos) {
                bin_file_name = file_name;
                break;
            }
        }
        if (bin_file_name.empty()) {
            SCP_FATAL(())
            ("Can't find any register binary reset values files in the zip archive: {}", get_archive_name());
        }
        if (zip_stat(m_archive, bin_file_name.c_str(), ZIP_FL_NOCASE, &z_stat) < 0) {
            SCP_FATAL(())
            ("Can't execute zip_stat() on {} in archive: {}", bin_file_name, get_archive_name());
        }
        return z_stat.size;
    }

    rapidjson::Document& get_json(std::string file, rapidjson::Document& d)
    {
        zip_stat_t stat;
        SCP_TRACE(())("Looking for {} in archive: {}", file.c_str(), get_archive_name());

        if (file.empty()) {
            file = get_json_topname() + ".json";
        }
        if (zip_stat(m_archive, file.c_str(), ZIP_FL_NOCASE, &stat) != 0) {
            SCP_DEBUG(())("Unable to find {} in archive: {}", file.c_str(), get_archive_name());
            return d;
        }

        char* json_info = (char*)malloc(stat.size + 1);
        zip_file_t* fd = zip_fopen(m_archive, file.c_str(), ZIP_FL_NOCASE);
        if (zip_fread(fd, json_info, stat.size) != stat.size) {
            SCP_FATAL(())("Unable to read {} in archive: {}", file, get_archive_name());
        }
        json_info[stat.size] = '\0';

        d.Parse(json_info);
        if (d.HasParseError()) {
            SCP_FATAL(())("Unable to parse {} in archive: {}", stat.name, get_archive_name());
        }
        zip_fclose(fd);
        free(json_info);
        return d;
    }

private:
    template <class T>
    void cci_set(cci::cci_broker_handle broker, std::string n, T value)
    {
        if (!broker.has_preset_value(n)) {
            SCP_DEBUG(())("set {} to {}", n, value);
            broker.set_preset_cci_value(n, cci::cci_value(value));
        } else {
            SCP_FATAL(())("Trying to re-set a value? {}", n);
        }
    }

public:
    bool json_read_cci(cci::cci_broker_handle broker, std::string sc_name, bool is_platform_model = false,
                       std::shared_ptr<gs::router<>> _router = nullptr,
                       std::shared_ptr<gs::gs_memory<>> _memory = nullptr, bool is_platform = false,
                       std::string _path = "", rapidjson::Value* _node = nullptr, bool top = true)
    {
        m_is_platform_model = is_platform_model;
        m_is_platform_top = is_platform;
        std::string module_path = _path.empty() ? get_json_topname() : _path;

        rapidjson::Document json_doc;
        rapidjson::Value& node = _node ? *_node : get_json(module_path + ".json", json_doc);

        if (node.IsNull()) return false;

        sc_name.erase(remove_if(sc_name.begin(), sc_name.end(), isspace), sc_name.end());
        if (m_loaded.count(sc_name)) return true;
        m_loaded.insert(sc_name);

        SCP_INFO(())
        ("JSON read into sc object {} from JSON node {}", sc_name, node["NAME"].GetString());

        if (node.HasMember("ADDRESS") && !m_base_address_captured) {
            m_router = _router;
            m_memory = _memory;
            uint64_t bin_file_size = get_bin_file_size();
            uint64_t size = 0;
            if (node.HasMember("SIZE")) size = getNumber(node["SIZE"]);
            m_max_size = std::max(size, bin_file_size);
            SCP_TRACE(())("Setting {}.reg_memory", sc_name);
            m_base_address = getNumber(node["ADDRESS"]);
            m_parent_model_base_addr = m_base_address;
            if (!is_platform) {
                std::string parent_mod_name = sc_name.substr(0, sc_name.find_last_of('.'));
                if (broker.has_preset_value(parent_mod_name + ".target_socket.address")) {
                    broker.get_preset_cci_value(parent_mod_name + ".target_socket.address")
                        .template try_get<uint64_t>(m_parent_model_base_addr);
                } else {
                    SCP_WARN(())
                    ("{} is not connected to a router, or not configured properly", parent_mod_name);
                }
            }
            m_base_address_captured = true;
        }
        if (node.HasMember("MODULES")) {
            auto mods = node["MODULES"].GetArray();
            for (auto& mod : mods) {
                if (mod.IsObject()) {
                    if (!mod.HasMember("INST_HIER")) {
                        SCP_FATAL(())
                        ("Missing INST_HIER entry while parsing JSON config file in archive: {}", get_archive_name());
                    }
                    std::string mod_hier_name = mod["INST_HIER"].GetString();
                    std::string mod_name = mod["NAME"].GetString();
                    std::string sn = sc_name + "." +
                                     (is_platform ? mod_hier_name.substr(mod_hier_name.find_last_of(".") + 1)
                                                  : mod_name);
                    json_read_cci(broker, sn, is_platform_model, _router, _memory, is_platform, module_path, &mod,
                                  false);
                    if (mod.HasMember("OFFSET")) {
                        cci_set(broker, sn + ".target_socket.chained", false);
                    }
                }
            }
        }
        if (node.HasMember("MEMORY") && !is_platform_model) {
            uint64_t address = getNumber(node["ADDRESS"]);
            auto mems = node["MEMORY"].GetArray();
            for (auto& m : mems) {
                std::string sn = sc_name + "." + m["NAME"].GetString();
                uint64_t mem_address = 0;
                if (m.HasMember("OFFSET"))
                    mem_address = getNumber(m["OFFSET"]) + (is_platform ? address : 0);
                else
                    mem_address = getNumber(m["ADDRESS"]) + (is_platform ? address : 0);
                uint64_t mem_size = (m.HasMember("SIZE") ? getNumber(m["SIZE"]) : 0);
                m_max_size = std::max(m_max_size, (mem_address + mem_size - (is_platform ? m_base_address : 0)));
                cci_set(broker, sn + ".target_socket.address", mem_address);
                cci_set(broker, sn + ".target_socket.size", mem_size);
                cci_set(broker, sn + ".target_socket.relative_addresses", true);
                cci_set(broker, sn + ".target_socket.priority", 0);
            }
        }
        if (node.HasMember("ADDRESS")) {
            uint64_t mod_size = 0;
            uint64_t mod_address = getNumber(node["ADDRESS"]);
            if (node.HasMember("SIZE")) mod_size = getNumber(node["SIZE"]);
            m_max_size = std::max(m_max_size, (mod_address + mod_size - m_base_address));
            cci_set(broker, sc_name + ".target_socket.address", mod_address);
            cci_set(broker, sc_name + ".target_socket.size", mod_size);
            cci_set(broker, sc_name + ".target_socket.relative_addresses", true);
            if (!is_platform) cci_set(broker, sc_name + ".router.lazy_init", true);
        }
        if (top) {
            if (!is_platform_model && !is_platform) {
                cci_set(broker, sc_name + ".reg_memory.target_socket.address", 0);
                cci_set(broker, sc_name + ".reg_memory.target_socket.size", m_max_size);
                cci_set(broker, sc_name + ".reg_memory.target_socket.relative_addresses", true);
            }
            cci_set(broker, sc_name + ".reg_router.target_socket.address",
                    (is_platform_model || is_platform) ? m_parent_model_base_addr : 0);
            cci_set(broker, sc_name + ".reg_router.target_socket.size", m_max_size);
            cci_set(broker, sc_name + ".reg_router.target_socket.priority", 1024);
            cci_set(broker, sc_name + ".reg_router.target_socket.relative_addresses", true);
            cci_set(broker, sc_name + ".reg_router.lazy_init", true);
        }
        return true;
    }

    uint64_t get_base_address() const { return m_base_address; }
    uint64_t get_parent_module_address() { return m_parent_model_base_addr; }
    bool is_platform_top() { return m_is_platform_top; }
    bool is_platform_model() { return m_is_platform_model; }
    void set_memory(std::shared_ptr<gs::gs_memory<>> _memory) { m_memory = _memory; }
    void set_router(std::shared_ptr<gs::router<>> _router) { m_router = _router; }
    std::shared_ptr<gs::gs_memory<>> get_memory() { return m_memory; }
    std::shared_ptr<gs::router<>> get_router() { return m_router; }
};

class base_components
{
    SCP_LOGGER((), "json_module_base_components");

public:
    base_components(const std::string& name, json_zip_archive& jza, cci::cci_broker_handle broker,
                    gs::router<>* _router, gs::gs_memory<>* _reg_memory)
        : m_router(nullptr)
        , m_reg_memory(nullptr)
        , m_reg_router(nullptr)
        , m_reset("reset")
        , m_is_platform_mod(_router && _reg_memory)
        , m_is_platform_top(jza.is_platform_top())
    {
        SCP_LOGGER_NAME().features[0] = "json_module_base_components";
        SCP_TRACE(())("base_components Constructor");
        if (!m_is_platform_mod) {
            // use raw pointer as m_router & m_reg_memory may not be created/managed by base_components
            if (!m_is_platform_top) {
                m_reg_memory = std::make_shared<gs::gs_memory<>>("reg_memory");
                m_router = std::make_shared<gs::router<>>("router");
                m_reset.bind(m_reg_memory->reset);
                bin_file_name = jza.get_bin_file_name();
                zip_archive_name = jza.get_archive_name();
                zip_archive_ptr = jza.get_zip_archive_ptr();
                m_reg_memory->load.zip_file_load(zip_archive_ptr, zip_archive_name, 0, bin_file_name);
            } else {
                m_router = jza.get_router();
                m_reg_memory = jza.get_memory();
            }
        } else {
            std::string mod_name = std::string(_router->name());
            std::string container_name = mod_name.substr(0, mod_name.find_last_of("."));
            gs::ModuleFactory::ContainerBase* container = dynamic_cast<gs::ModuleFactory::ContainerBase*>(
                gs::find_sc_obj(nullptr, container_name));
            if (!container) {
                SCP_ERR(())
                ("parent module: {} is not a container)", container_name);
            }
            m_router = std::dynamic_pointer_cast<gs::router<>>(
                container->find_module_by_name(std::string(_router->name())));
            m_reg_memory = std::dynamic_pointer_cast<gs::gs_memory<>>(
                container->find_module_by_name(std::string(_reg_memory->name())));
        }
        sc_assert(m_router);
        sc_assert(m_reg_memory);
    }
    void insert_mod_addr_name_pair(const std::pair<uint64_t, std::pair<uint64_t, std::string>>& item)
    {
        m_mod_addr_name_map.insert(item);
    }
    void init_reg_router()
    {
        if (m_reg_memory && !m_reg_router) {
            m_reg_router = std::make_unique<gs::reg_router<>>("reg_router", m_mod_addr_name_map);
            m_reg_router->initiator_socket(m_reg_memory->socket);
            if (!m_is_platform_mod) m_router->initiator_socket(m_reg_router->target_socket);
        }
    }
    ~base_components() {}
    void reset(bool value)
    {
        if (m_reg_memory && !m_is_platform_mod && !m_is_platform_top) {
            m_reset->write(value);
            if (value) {
                m_reg_memory->load.zip_file_load(zip_archive_ptr, zip_archive_name, 0, bin_file_name);
            }
        }
    }

    std::shared_ptr<gs::router<>> m_router;
    std::shared_ptr<gs::gs_memory<>> m_reg_memory;
    std::unique_ptr<gs::reg_router<>> m_reg_router;

private:
    InitiatorSignalSocket<bool> m_reset;
    std::map<uint64_t, std::pair<uint64_t, std::string>> m_mod_addr_name_map;
    bool m_is_platform_mod;
    bool m_is_platform_top;
    std::string bin_file_name;
    std::string zip_archive_name;
    zip_t* zip_archive_ptr;
};

class json_module_base : public virtual sc_core::sc_module
{
    SCP_LOGGER();

protected:
    json_zip_archive& m_jza;
    cci::cci_broker_handle m_broker;
    std::shared_ptr<base_components> m_sh_comp;
    sc_core::sc_vector<gs::gs_memory<>> memories;
    sc_core::sc_vector<json_module_base> sub_modules;
    uint32_t m_bound_regs_num;
    bool m_is_platform_mod;

protected:
    template <class T>
    void cci_set(std::string n, T value)
    {
        auto handle = m_broker.get_param_handle(n);
        if (handle.is_valid())
            handle.set_cci_value(cci::cci_value(value));
        else if (!m_broker.has_preset_value(n)) {
            m_broker.set_preset_cci_value(n, cci::cci_value(value));
        } else {
            SCP_FATAL(())("Trying to re-set a value? {}", n);
        }
    }

    template <typename T>
    T cci_get(std::string n)
    {
        T ret;
        if (!m_broker.get_preset_cci_value(n).template try_get<T>(ret)) {
            SCP_FATAL(()) << "Unable to get parameter " << n << "\nIs your .lua file up-to-date?";
        };
        m_broker.lock_preset_value(n);
        return ret;
    }

public:
    ~json_module_base() = default;

    json_module_base(sc_core::sc_module_name _name, json_zip_archive& jza, gs::router<>* _router = nullptr,
                     gs::gs_memory<>* _reg_memory = nullptr, const std::shared_ptr<base_components>& sh_comp = nullptr,
                     rapidjson::Value* _node = nullptr, std::string _path = "", bool top = true)
        : sc_core::sc_module(_name)
        , m_jza(jza)
        , m_broker(cci::cci_get_broker())
        , m_sh_comp(sh_comp)
        , m_bound_regs_num(0)
        , m_is_platform_mod(false)
    {
        SCP_TRACE(())("Constructor of {}", sc_core::sc_module::name());
        std::string n = std::string(sc_core::sc_module::name());
        std::string module_path = _path.empty() ? jza.get_json_topname() : _path;
        rapidjson::Document json_doc;
        rapidjson::Value& node = _node ? *_node : jza.get_json(module_path + ".json", json_doc);

        if (!m_sh_comp) {
            m_is_platform_mod = jza.is_platform_model();
            if (m_is_platform_mod && !_router && !_reg_memory) {
                SCP_FATAL(())
                ("Platform model is instantiated without router and reg_memory arguments, is your conf.lua updated?");
            }
            m_sh_comp = std::make_shared<base_components>(std::string(sc_core::sc_module::name()), jza, m_broker,
                                                          _router, _reg_memory);
        }
        if (m_sh_comp && jza.is_platform_top() && node.HasMember("ADDRESS") && node.HasMember("SIZE") &&
            node.HasMember("INST_HIER")) {
            std::string mod_hier_name = node["INST_HIER"].GetString();
            std::string mod_name = mod_hier_name.substr(mod_hier_name.find_last_of(".") + 1);
            m_sh_comp->insert_mod_addr_name_pair(std::make_pair<uint64_t, std::pair<uint64_t, std::string>>(
                (jza.getNumber(node["ADDRESS"]) - jza.get_base_address()) +
                    (m_is_platform_mod ? jza.get_parent_module_address() : 0),
                std::make_pair<uint64_t, std::string>(jza.getNumber(node["SIZE"]), std::move(mod_name))));
        }
        if (!m_is_platform_mod && node.HasMember("MEMORY")) {
            auto mems = node["MEMORY"].GetArray();
            memories.init(mems.Size(), [&, mems](const char* name, size_t n) -> gs::gs_memory<>* {
                return new gs::gs_memory<>(mems[n]["NAME"].GetString());
            });
        }
        for (auto& m : memories) {
            m_sh_comp->m_router->initiator_socket(m.socket);
        }
        if (node.HasMember("MODULES")) {
            auto mods = node["MODULES"].GetArray();
            sub_modules.init(mods.Size(), [&, mods](const char* name, size_t n) -> json_module_base* {
                if (!mods[n].IsObject()) {
                    SCP_FATAL(())("JSON file for json_module_base must be fully self contained");
                }
                std::string mod_hier_name = mods[n]["INST_HIER"].GetString();
                std::string mod_name = mods[n]["NAME"].GetString();
                mod_hier_name.erase(remove_if(mod_hier_name.begin(), mod_hier_name.end(), isspace),
                                    mod_hier_name.end());
                mod_name.erase(remove_if(mod_name.begin(), mod_name.end(), isspace), mod_name.end());
                std::string used_mod_name = (jza.is_platform_top()
                                                 ? mod_hier_name.substr(mod_hier_name.find_last_of(".") + 1)
                                                 : mod_name);
                return new json_module_base(used_mod_name.c_str(), jza, _router, _reg_memory, m_sh_comp, &mods[n],
                                            module_path, false);
            });
        }
        if (top) {
            m_sh_comp->init_reg_router();
        }
    }

    template <class TYPE>
    void bind_reg(gs::gs_register<TYPE>& reg)
    {
        std::string path = reg.get_path();
        std::string self_full_name = std::string(sc_core::sc_module::name());
        std::string self_name = self_full_name.substr(self_full_name.find_last_of(".") + 1);
        std::string parent = path.substr(self_name.length() + 1);
        if (parent == reg.get_regname()) {
            parent = "";
        } else {
            parent = parent.substr(0, parent.find_last_of("."));
        }
        SCP_TRACE(())
        ("Binding {} in {}", reg.get_regname(),
         std::string(sc_core::sc_module::name()) + ((!parent.empty()) ? "." + parent : ""));
        reg.initiator_socket.bind(m_sh_comp->m_reg_memory->socket);
        std::string mod_address_str = std::string(sc_core::sc_module::name()) + "." +
                                      ((!parent.empty()) ? parent + "." : "") + "target_socket.address";
        std::string reg_target_socket_name = std::string(this->name()) + "." + ((!parent.empty()) ? parent + "." : "") +
                                             reg.get_regname() + ".target_socket";
        uint64_t parent_address = m_jza.get_parent_module_address();
        uint64_t mod_address = cci_get<uint64_t>(mod_address_str) - m_jza.get_base_address();
        std::string reg_address_str = reg_target_socket_name + ".address";
        uint64_t new_reg_offset = reg.get_offset() + mod_address + (m_jza.is_platform_model() ? parent_address : 0);
        cci_set<uint64_t>(reg_address_str, new_reg_offset);
        m_sh_comp->m_reg_router->initiator_socket.bind(reg);
        m_sh_comp->m_reg_router->rename_last(reg_target_socket_name);
        m_bound_regs_num++;
    }

    uint32_t get_num_of_bound_regs() const { return m_bound_regs_num; }

    void log_end_of_binding_msg(const std::string& gen_name)
    {
        SCP_DEBUG(()) << gen_name << ": Constructed and bound " << m_bound_regs_num << " registers.";
    }

    void reset(bool value) { m_sh_comp->reset(value); }
};

class json_module : public json_module_base
{
public:
    tlm_utils::multi_passthrough_target_socket<json_module, DEFAULT_TLM_BUSWIDTH> target_socket;

    ~json_module() = default;

    json_module(sc_core::sc_module_name _name, json_zip_archive& jza, gs::router<>* _router = nullptr,
                gs::gs_memory<>* _reg_memory = nullptr, const std::shared_ptr<base_components>& sh_comp = nullptr,
                rapidjson::Value* _node = nullptr, std::string _path = "", bool top = true)
        : json_module_base(_name, jza, _router, _reg_memory, sh_comp, _node, _path, top), target_socket("target_socket")
    {
        if (!_router && !_reg_memory) {
            target_socket.bind(m_sh_comp->m_router->target_socket);
        } else {
            target_socket.bind(m_sh_comp->m_reg_router->target_socket);
        }
    }
};

class json_module_top : public json_module_base
{
private:
    bool is_json_loaded_ok(json_zip_archive& jza, cci::cci_broker_handle broker, std::string sc_name,
                           std::shared_ptr<gs::router<>> _router, std::shared_ptr<gs::gs_memory<>> _memory,
                           std::string _path, rapidjson::Value* _node)
    {
        bool loaded_ok = jza.json_read_cci(broker, sc_name, false, _router, _memory, true, _path, _node, true);
        sc_assert(loaded_ok);
        return true;
    }

public:
    ~json_module_top() = default;

    json_module_top(sc_core::sc_module_name _name, json_zip_archive& jza, cci::cci_broker_handle broker,
                    std::string sc_name, std::shared_ptr<gs::router<>> _router = nullptr,
                    std::shared_ptr<gs::gs_memory<>> _memory = nullptr, std::string _path = "",
                    rapidjson::Value* _node = nullptr)
        : json_module_base(_name, jza, nullptr, nullptr, nullptr, nullptr, "",
                           is_json_loaded_ok(jza, broker, sc_name, _router, _memory, _path, _node))
    {
    }
};

} // namespace gs

#define TXN(T) tlm::tlm_generic_payload &FIRST_ARG(T, _txn), sc_core::sc_time &delay

#endif // _REG_MODEL_MAKER_H