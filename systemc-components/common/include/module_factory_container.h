/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef GREENSOCS_MODULE_FACTORY_H
#define GREENSOCS_MODULE_FACTORY_H

#include "cciutils.h"
#include <cci_configuration>
#include <systemc>
#include <tlm>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include <dynlib_loader.h>
#include <scp/report.h>
#include <scp/helpers.h>

#include <libgsutils.h>
#include <libgssync.h>

#include <map>

#include <ports/target-signal-socket.h>
#include <ports/initiator-signal-socket.h>

#ifndef WITHOUT_QEMU
#include <ports/target.h>
#include <ports/initiator.h>
#include <ports/qemu-target-signal-socket.h>
#include <ports/qemu-initiator-signal-socket.h>
#endif

#include <ports/biflow-socket.h>
#include <module_factory_registery.h>
#include <transaction_forwarder_if.h>
#include <tlm_sockets_buswidth.h>
#include <algorithm>
#include <vector>
#include <iterator>
#include <regex>
#include <cctype>

namespace gs {
namespace ModuleFactory {

class ContainerBase : public virtual sc_core::sc_module, public transaction_forwarder_if<CONTAINER>
{
    /**
     * @brief construct a module using the pre-register CCI functor, with typed arguments from a CCI
     * value list. The functor is expected to call new.
     *
     * @param moduletype The name of the type of module to be constructed
     * @param name The name to be given to this instance of the module
     * @param args The CCI list of arguments passed to the constructor (which will be type punned)
     * @return sc_core::sc_module* The module constructed.
     */

private:
    // Performance optimization caches (lazy initialization)
    mutable std::map<std::string, cci::cci_value_list> m_module_args_cache;
    mutable std::map<std::string, bool> m_is_absolute_path_cache;
    mutable std::map<std::string, std::map<std::string, std::set<std::string>>>
        m_bind_refs_cache; // [module_name][target] -> referrers
    mutable std::map<std::string, std::vector<std::pair<std::string, cci::cci_value>>>
        m_moduletype_params_cache; // container_full_name -> params
    mutable std::map<std::string, std::set<std::string>>
        m_per_pass_nested_deps_cache; // Per-pass cache: container_name -> deps (cleared each pass)
    mutable std::map<std::string, bool> m_is_container_type_cache; // Cache for is_container_type() results

public:
    SCP_LOGGER(());

    sc_core::sc_module* construct_module(std::string moduletype, sc_core::sc_module_name name, cci::cci_value_list args)
    {
        gs::cci_constructor_vl m_fac;
        cci::cci_value v = cci::cci_value::from_json("\"" + moduletype + "\"");
        if (!v.try_get<gs::cci_constructor_vl>(m_fac)) {
            register_module_from_dylib(name);
        }

        if (v.try_get<gs::cci_constructor_vl>(m_fac)) {
            /**
             * (m_fac)(name, args) will return raw pointer of unmanaged dynamically allocated moduletype:
             *  new moduletype(name, args)
             */
            sc_core::sc_module* mod = (m_fac)(name, args);
            if (mod) {
                m_constructedModules.emplace_back(mod);
                return mod;
            } else {
                SC_REPORT_ERROR("ModuleFactory", ("Null module type: " + moduletype).c_str());
            }
        } else {
            SC_REPORT_ERROR("ModuleFactory", ("Can't find module type: " + moduletype).c_str());
        }
        __builtin_unreachable();
    }

    /**
     * @brief Helper to construct a module that takes no arguments.
     *
     */
    sc_core::sc_module* construct_module(std::string moduletype, sc_core::sc_module_name name)
    {
        cci::cci_value_list emptyargs;
        return construct_module(moduletype, name, emptyargs);
    }

    std::string get_parent_name(const std::string& module_name)
    {
        return module_name.substr(0, module_name.find_last_of("."));
    }

    /**
     * @brief Check if a name refers to an absolute path (outside this container)
     *
     * @param name The name to check (without leading '&')
     * @return true if the path is absolute (not local to this container)
     */
    bool is_absolute_path(const std::string& name)
    {
        // Check cache first
        auto it = m_is_absolute_path_cache.find(name);
        if (it != m_is_absolute_path_cache.end()) {
            return it->second;
        }

        bool result = false;
        std::string container_name = std::string(this->name());

        // If the name starts with the container's name followed by a dot, it's relative
        if (name.size() > container_name.size() && name.substr(0, container_name.size()) == container_name &&
            name[container_name.size()] == '.') {
            result = false;
        } else if (name.find('.') != std::string::npos) {
            // If the name contains dots, it's a hierarchical path
            // Get the parent container name (e.g., "parent" from "parent.child_container")
            std::string parent_name = get_parent_name(container_name);
            if (parent_name.empty()) {
                // We're at the top level, everything with dots is relative to us
                result = false;
            } else if (name.find(parent_name + ".") == 0) {
                // Check if the name starts with the parent or any ancestor
                // If it starts with parent name, it's an absolute path from parent's perspective
                result = true;
            } else {
                // Check if it exists in the global hierarchy
                sc_core::sc_object* obj = find_sc_obj(nullptr, name, true);
                if (obj != nullptr) {
                    // Object exists, check if it's inside this container
                    std::string obj_name = std::string(obj->name());
                    result = obj_name.find(container_name + ".") != 0;
                } else {
                    result = false;
                }
            }
        } else {
            // For simple names (no dots), check if they exist in global hierarchy
            result = find_sc_obj(nullptr, name, true) != nullptr;
        }

        // Cache the result
        m_is_absolute_path_cache[name] = result;
        return result;
    }

    bool is_container_arg_mod_name(const std::string& name)
    {
        if (!this->container_mod_arg) return false;
        std::string arg_mod_full_name_str = std::string(this->container_mod_arg->name());
        std::string arg_mod_name_str = arg_mod_full_name_str.substr(get_parent_name(arg_mod_full_name_str).size() + 1);
        return name == arg_mod_name_str;
    }

    /**
     * @brief Get the module args helper to find argument list for a module constructor
     *
     * @param modulename name of the module from which to get the .arg[...] list
     * @return cci::cci_value_list
     */
    cci::cci_value_list get_module_args(std::string modulename)
    {
        // Check cache first
        auto cache_it = m_module_args_cache.find(modulename);
        if (cache_it != m_module_args_cache.end()) {
            return cache_it->second;
        }

        cci::cci_value_list args;
        for (int i = 0; cci::cci_get_broker().has_preset_value(modulename + ".args." + std::to_string(i)); i++) {
            SCP_TRACE(())("Looking for : {}.args.{}", modulename, std::to_string(i));

            gs::cci_clear_unused(m_broker, modulename + ".args." + std::to_string(i));
            auto arg = cci::cci_get_broker().get_preset_cci_value(modulename + ".args." + std::to_string(i));
            if (arg.is_string() && std::string(arg.get_string())[0] == '&') {
                std::string arg_path = std::string(arg.get_string()).substr(1); // Remove leading '&'

                // Check if it's an absolute path (exists in global hierarchy)
                if (!is_absolute_path(arg_path)) {
                    // It's a relative path, prepend the parent
                    std::string parent = is_container_arg_mod_name(arg_path)
                                             ? get_parent_name(std::string(this->container_mod_arg->name()))
                                             : get_parent_name(modulename);
                    // Only prepend if parent is not already in the path
                    if (arg_path.find(parent) == std::string::npos) {
                        arg.set_string("&" + parent + "." + arg_path);
                    } else {
                        arg.set_string("&" + arg_path);
                    }
                } else {
                    // It's an absolute path, use as-is
                    arg.set_string("&" + arg_path);
                }
            }
            args.push_back(arg);
        }

        // Cache the result
        m_module_args_cache[modulename] = args;
        return args;
    }

    template <typename I, typename T>
    bool try_bind(sc_core::sc_object* i_obj, sc_core::sc_object* t_obj)
    {
        auto i = dynamic_cast<I*>(i_obj);
        auto t = dynamic_cast<T*>(t_obj);
        if (i && t) {
            i->bind(*t);
            SCP_INFO(())
            ("Binding initiator socket: {}({}) with the target socket: {}({})", i_obj->name(), typeid(i).name(),
             t_obj->name(), typeid(t).name());
            return true;
        }
        t = dynamic_cast<T*>(i_obj);
        i = dynamic_cast<I*>(t_obj);
        if (i && t) {
            i->bind(*t);
            SCP_INFO(())
            ("Binding initiator socket: {}({}) with the target socket: {}({})", t_obj->name(), typeid(i).name(),
             i_obj->name(), typeid(t).name());
            return true;
        }
        return false;
    }

    /* bind all know port types */
    /**
     * @brief Bind all known port types
     *
     * @param modulename The LOCAL name of the module to bind it's ports
     * @param type The type of the module
     * @param m The module itself.
     */
    void name_bind(sc_core::sc_module* m)
    {
        for (auto param : sc_cci_children(m->name())) {
            /* We should have a param like
             *  foo = &a.baa
             * foo should relate to an object in this model. a.b.bar should relate to an other
             * object Note that the names are relative to the current module (this).
             */
            std::string targetname = std::string(m->name()) + "." + param;
            gs::cci_clear_unused(m_broker, targetname + ".0");
            if (m_broker.get_preset_cci_value(targetname + ".0").is_string()) {
                // deal with an alias
                std::string src = m_broker.get_preset_cci_value(targetname + ".0").get_string();
                m_broker.set_preset_cci_value(targetname + ".address", m_broker.get_preset_cci_value(src + ".address"));
                m_broker.set_preset_cci_value(targetname + ".size", m_broker.get_preset_cci_value(src + ".size"));
            }
            std::string bindname = targetname + ".bind";
            gs::cci_clear_unused(m_broker, bindname);
            if (!m_broker.get_preset_cci_value(bindname).is_string()) {
                continue;
            }
            std::string initname = m_broker.get_preset_cci_value(bindname).get_string();

            if (initname.at(0) != '&') continue; // all port binds are donated with a '&', so not for us

            if (initname.find(';') != std::string::npos) {
                order_bind(m, initname, targetname);
            } else {
                initname.erase(0, 1); // remove leading '&'
                // Check if it's an absolute path (exists in global hierarchy)
                if (!is_absolute_path(initname)) {
                    // It's a relative path, prepend the container name
                    initname = std::string(name()) + "." + initname;
                }
                SCP_TRACE(())("Bind {} to {}", targetname, initname);
                do_binding(m, initname, targetname);
            }
        }
    }

    /**
     * FIXME: There should be a better way of doing that.Trying to bind every possible
     * combination of socket types is not scalable nor readable solution.
     *
     */
    template <unsigned int BIND_BUSWIDTH>
    bool try_bind_all(sc_core::sc_object* i_obj, sc_core::sc_object* t_obj)
    {
        if ((try_bind<tlm_utils::multi_init_base<BIND_BUSWIDTH>, tlm::tlm_base_target_socket<BIND_BUSWIDTH>>(i_obj,
                                                                                                             t_obj)) ||
            (try_bind<tlm_utils::multi_init_base<BIND_BUSWIDTH>, tlm_utils::multi_target_base<BIND_BUSWIDTH>>(i_obj,
                                                                                                              t_obj)) ||
            (try_bind<tlm::tlm_base_initiator_socket<BIND_BUSWIDTH>, tlm::tlm_base_target_socket<BIND_BUSWIDTH>>(
                i_obj, t_obj)) ||
            (try_bind<tlm::tlm_base_initiator_socket<BIND_BUSWIDTH>, tlm_utils::multi_target_base<BIND_BUSWIDTH>>(
                i_obj, t_obj)) ||
            (try_bind<tlm::tlm_base_initiator_socket<BIND_BUSWIDTH, tlm::tlm_fw_transport_if<>,
                                                     tlm::tlm_bw_transport_if<>, 1, sc_core::SC_ZERO_OR_MORE_BOUND>,
                      tlm_utils::multi_target_base<BIND_BUSWIDTH>>(i_obj, t_obj)) ||
            (try_bind<gs::biflow_bindable, gs::biflow_bindable>(i_obj, t_obj)) ||
            (try_bind<tlm::tlm_base_initiator_socket<BIND_BUSWIDTH, tlm::tlm_fw_transport_if<>,
                                                     tlm::tlm_bw_transport_if<>, 1, sc_core::SC_ZERO_OR_MORE_BOUND>,
                      tlm::tlm_base_target_socket<BIND_BUSWIDTH>>(i_obj, t_obj)) ||
            (try_bind<
                tlm::tlm_initiator_socket<BIND_BUSWIDTH, tlm::tlm_base_protocol_types, 1,
                                          sc_core::SC_ONE_OR_MORE_BOUND>,
                tlm::tlm_target_socket<BIND_BUSWIDTH, tlm::tlm_base_protocol_types, 1, sc_core::SC_ZERO_OR_MORE_BOUND>>(
                i_obj, t_obj)) ||
            (try_bind<
                tlm_utils::multi_init_base<BIND_BUSWIDTH>,
                tlm::tlm_target_socket<BIND_BUSWIDTH, tlm::tlm_base_protocol_types, 1, sc_core::SC_ZERO_OR_MORE_BOUND>>(
                i_obj, t_obj)) ||
            (try_bind<
                tlm::tlm_base_initiator_socket<BIND_BUSWIDTH, tlm::tlm_fw_transport_if<>, tlm::tlm_bw_transport_if<>, 1,
                                               sc_core::SC_ZERO_OR_MORE_BOUND>,
                tlm::tlm_target_socket<BIND_BUSWIDTH, tlm::tlm_base_protocol_types, 1, sc_core::SC_ZERO_OR_MORE_BOUND>>(
                i_obj, t_obj)) ||
            (try_bind<sc_core::sc_port<sc_core::sc_signal_inout_if<bool>, 0, sc_core::SC_ZERO_OR_MORE_BOUND>,
                      TargetSignalSocket<bool>>(i_obj, t_obj)) ||
            (try_bind<InitiatorSignalSocket<bool>, TargetSignalSocket<bool>>(i_obj, t_obj)) ||
            (try_bind<InitiatorSignalSocket<uint32_t>, TargetSignalSocket<uint32_t>>(i_obj, t_obj))
#ifndef WITHOUT_QEMU
            ||
            (try_bind<QemuInitiatorSocket<BIND_BUSWIDTH>, tlm::tlm_base_target_socket<BIND_BUSWIDTH>>(i_obj, t_obj)) ||
            (try_bind<QemuInitiatorSocket<BIND_BUSWIDTH>, tlm_utils::multi_target_base<BIND_BUSWIDTH>>(i_obj, t_obj)) ||
            (try_bind<tlm_utils::multi_init_base<BIND_BUSWIDTH>, QemuTargetSocket<BIND_BUSWIDTH>>(i_obj, t_obj)) ||
            (try_bind<tlm::tlm_base_initiator_socket<BIND_BUSWIDTH>, QemuTargetSocket<BIND_BUSWIDTH>>(i_obj, t_obj)) ||
            (try_bind<QemuInitiatorSocket<BIND_BUSWIDTH>, QemuTargetSocket<BIND_BUSWIDTH>>(i_obj, t_obj)) ||
            (try_bind<QemuInitiatorSignalSocket, QemuTargetSignalSocket>(i_obj, t_obj)) ||
            (try_bind<QemuInitiatorSignalSocket, TargetSignalSocket<bool>>(i_obj, t_obj)) ||
            (try_bind<InitiatorSignalSocket<bool>, QemuTargetSignalSocket>(i_obj, t_obj))
#endif
        ) {
            return true;
        } else {
            return false;
        }
    }

    /**
     * @brief Check if a module is a container type
     *
     * @param name Module name (without parent container prefix)
     * @return true if the module is a container type
     */
    bool is_container_type(const std::string& name)
    {
        // Check cache first
        auto cache_it = m_is_container_type_cache.find(name);
        if (cache_it != m_is_container_type_cache.end()) {
            return cache_it->second;
        }

        std::string module_name = std::string(sc_module::name());
        std::string mod_type_name = module_name + "." + name + ".moduletype";

        bool result = false;
        if (m_broker.has_preset_value(mod_type_name)) {
            cci::cci_value cci_type = m_broker.get_preset_cci_value(mod_type_name);
            if (cci_type.is_string()) {
                std::string mod_type = cci_type.get_string();
                result = (mod_type == "Container" || mod_type == "ContainerWithArgs" ||
                          mod_type == "ContainerDeferModulesConstruct" || mod_type == "container_builder");
            }
        }

        // Cache the result
        m_is_container_type_cache[name] = result;
        return result;
    }

    /**
     * @brief Get all EXTERNAL nested dependencies from a container's children
     * (excludes dependencies that are internal to the container being checked)
     * Uses CCI configuration parameters to find child modules, then get_module_args for path resolution
     * Also checks config tables for container_builder types
     *
     * @param container_name Container name (without parent prefix)
     * @param dependencies Set to populate with dependency paths
     */
    void get_nested_dependencies(const std::string& container_name, std::set<std::string>& dependencies)
    {
        auto cache_it = m_per_pass_nested_deps_cache.find(container_name);
        if (cache_it != m_per_pass_nested_deps_cache.end()) {
            dependencies.insert(cache_it->second.begin(), cache_it->second.end());
            return;
        }

        std::string module_name = std::string(sc_module::name());
        std::string container_full_name = module_name + "." + container_name;
        std::set<std::string> computed_deps;

        std::vector<std::pair<std::string, cci::cci_value>> all_params;
        auto params_cache_it = m_moduletype_params_cache.find(container_full_name);
        if (params_cache_it != m_moduletype_params_cache.end()) {
            all_params = params_cache_it->second;
        } else {
            auto params_range = m_broker.get_unconsumed_preset_values(
                [&container_full_name](const std::pair<std::string, cci::cci_value>& iv) {
                    return iv.first.find(container_full_name + ".") == 0 &&
                           iv.first.find(".moduletype") != std::string::npos &&
                           iv.first.find(".moduletype") == iv.first.length() - 11;
                });
            for (const auto& param : params_range) {
                all_params.push_back(param);
            }
            m_moduletype_params_cache[container_full_name] = all_params;
        }

        std::set<std::string> child_modules;
        bool config_prefix_used = false;
        std::string config_prefix = container_full_name + ".config.";

        for (const auto& param : all_params) {
            std::string param_name = param.first;
            if (param_name == container_full_name + ".moduletype") continue;

            if (param_name.find(config_prefix) == 0 && param_name.find(".moduletype") == param_name.length() - 11) {
                std::string child_path = param_name.substr(config_prefix.length());
                child_path = child_path.substr(0, child_path.length() - 11);
                size_t dot_pos = child_path.find('.');
                if (dot_pos != std::string::npos) {
                    child_path = child_path.substr(0, dot_pos);
                }
                child_modules.insert(child_path);
                config_prefix_used = true;
            } else if (param_name.length() > container_full_name.length() + 12) {
                std::string child_path = param_name.substr(container_full_name.length() + 1);
                if (child_path.find("config.") == 0) continue;

                child_path = child_path.substr(0, child_path.length() - 11);
                size_t dot_pos = child_path.find('.');
                if (dot_pos != std::string::npos) {
                    child_path = child_path.substr(0, dot_pos);
                }
                child_modules.insert(child_path);
            }
        }

        for (const auto& child : child_modules) {
            std::string child_config_name;
            if (config_prefix_used) {
                child_config_name = config_prefix + child;
            } else {
                child_config_name = container_full_name + "." + child;
            }

            cci::cci_value_list child_args;
            auto args_cache_it = m_module_args_cache.find(child_config_name);
            if (args_cache_it != m_module_args_cache.end()) {
                child_args = args_cache_it->second;
            } else {
                for (int i = 0; m_broker.has_preset_value(child_config_name + ".args." + std::to_string(i)); i++) {
                    child_args.push_back(
                        m_broker.get_preset_cci_value(child_config_name + ".args." + std::to_string(i)));
                }
                m_module_args_cache[child_config_name] = child_args;
            }

            for (auto arg : child_args) {
                if (arg.is_string()) {
                    std::string a = arg.get_string();
                    if (a.length() > 0 && a[0] == '&') {
                        std::string ref_path = a.substr(1);

                        if (ref_path.find(module_name) != 0) {
                            ref_path = container_full_name + "." + ref_path;
                        }

                        if (!(ref_path.length() >= container_full_name.size() &&
                              ref_path.substr(0, container_full_name.size()) == container_full_name &&
                              (ref_path.length() == container_full_name.size() ||
                               ref_path[container_full_name.size()] == '.'))) {
                            computed_deps.insert(ref_path);
                        }
                    }
                }
            }

            // Check bind parameters only if this child is a container type
            bool child_is_container = false;
            auto container_cache_it = m_is_container_type_cache.find(child);
            if (container_cache_it != m_is_container_type_cache.end()) {
                child_is_container = container_cache_it->second;
            } else {
                if (m_broker.has_preset_value(child_config_name + ".moduletype")) {
                    cci::cci_value mt = m_broker.get_preset_cci_value(child_config_name + ".moduletype");
                    if (mt.is_string()) {
                        std::string moduletype = mt.get_string();
                        child_is_container = (moduletype == "Container" || moduletype == "container_builder");
                    }
                }
                m_is_container_type_cache[child] = child_is_container;
            }

            if (child_is_container) {
                // Get bind parameters for this container child
                auto child_params = m_broker.get_unconsumed_preset_values(
                    [&child_config_name](const std::pair<std::string, cci::cci_value>& iv) {
                        return iv.first.find(child_config_name + ".") == 0 &&
                               iv.first.find(".bind") != std::string::npos;
                    });

                for (const auto& param : child_params) {
                    if (param.second.is_string()) {
                        std::string bind_value = param.second.get_string();
                        // Handle semicolon-separated bind strings
                        size_t pos = 0;
                        while (pos < bind_value.length()) {
                            size_t end = bind_value.find(';', pos);
                            if (end == std::string::npos) end = bind_value.length();

                            std::string single_bind = bind_value.substr(pos, end - pos);
                            // Trim whitespace
                            size_t start = single_bind.find_first_not_of(" \t\n\r");
                            if (start != std::string::npos) {
                                size_t finish = single_bind.find_last_not_of(" \t\n\r");
                                single_bind = single_bind.substr(start, finish - start + 1);
                            }

                            if (single_bind.length() > 0 && single_bind[0] == '&') {
                                std::string ref_path = single_bind.substr(1);

                                // Make absolute if relative
                                if (ref_path.find(module_name) != 0) {
                                    ref_path = container_full_name + "." + ref_path;
                                }

                                // Only add if it's external to this container
                                if (!(ref_path.length() >= container_full_name.size() &&
                                      ref_path.substr(0, container_full_name.size()) == container_full_name &&
                                      (ref_path.length() == container_full_name.size() ||
                                       ref_path[container_full_name.size()] == '.'))) {
                                    computed_deps.insert(ref_path);
                                }
                            }

                            pos = (end == bind_value.length()) ? end : end + 1;
                        }
                    }
                }
            }
        }

        // Check the container's own bind parameters (outside config table)
        auto container_params = m_broker.get_unconsumed_preset_values(
            [&container_full_name](const std::pair<std::string, cci::cci_value>& iv) {
                std::string param_name = iv.first;
                // Match pattern: container_full_name.<param>.bind but not container_full_name.config.*
                return param_name.find(container_full_name + ".") == 0 &&
                       param_name.find(".config.") == std::string::npos &&
                       param_name.find(".bind") != std::string::npos;
            });

        for (const auto& param : container_params) {
            if (param.second.is_string()) {
                std::string bind_value = param.second.get_string();
                // Handle semicolon-separated bind strings
                size_t pos = 0;
                while (pos < bind_value.length()) {
                    size_t end = bind_value.find(';', pos);
                    if (end == std::string::npos) end = bind_value.length();

                    std::string single_bind = bind_value.substr(pos, end - pos);
                    // Trim whitespace
                    size_t start = single_bind.find_first_not_of(" \t\n\r");
                    if (start != std::string::npos) {
                        size_t finish = single_bind.find_last_not_of(" \t\n\r");
                        single_bind = single_bind.substr(start, finish - start + 1);
                    }

                    if (single_bind.length() > 0 && single_bind[0] == '&') {
                        std::string ref_path = single_bind.substr(1);

                        // Make absolute if relative
                        if (ref_path.find(module_name) != 0) {
                            ref_path = container_full_name + "." + ref_path;
                        }

                        // Only add if it's external to this container
                        if (!(ref_path.length() >= container_full_name.size() &&
                              ref_path.substr(0, container_full_name.size()) == container_full_name &&
                              (ref_path.length() == container_full_name.size() ||
                               ref_path[container_full_name.size()] == '.'))) {
                            computed_deps.insert(ref_path);
                        }
                    }

                    pos = (end == bind_value.length()) ? end : end + 1;
                }
            }
        }

        m_per_pass_nested_deps_cache[container_name] = computed_deps;
        dependencies.insert(computed_deps.begin(), computed_deps.end());
    }

    bool are_dependencies_available(const std::string& name, const std::set<std::string>& done)
    {
        std::string module_name = std::string(sc_module::name());
        std::set<std::string> all_dependencies;

        cci::cci_value_list mod_args = get_module_args(module_name + "." + name);
        for (auto arg : mod_args) {
            if (arg.is_string()) {
                std::string a = arg.get_string();
                if (a.length() > 0 && a[0] == '&') {
                    std::string ref_path = a.substr(1);
                    if (!ref_path.empty()) {
                        all_dependencies.insert(ref_path);
                    }
                }
            }
        }

        if (is_container_type(name)) {
            get_nested_dependencies(name, all_dependencies);
        }

        for (const auto& ref_path : all_dependencies) {
            if (ref_path.substr(0, module_name.size()) == module_name && ref_path.length() > module_name.size() &&
                ref_path[module_name.size()] == '.') {
                std::string referenced_module = ref_path.substr(module_name.size() + 1);
                size_t dot_pos = referenced_module.find('.');
                if (dot_pos != std::string::npos) {
                    referenced_module = referenced_module.substr(0, dot_pos);
                }
                if (done.count(referenced_module) == 0) {
                    return false;
                }
            } else {
                sc_core::sc_object* dep_obj = find_sc_obj(nullptr, ref_path, true);
                if (dep_obj == nullptr) {
                    return false;
                }
            }
        }
        return true;
    }

    // Find modules with bind parameters pointing to target_module
    std::set<std::string> get_bind_references_to_module(const std::string& module_name,
                                                        const std::string& target_module_name,
                                                        const std::list<std::string>& all_modules)
    {
        // Check cache first
        auto& module_cache = m_bind_refs_cache[module_name];
        auto cache_it = module_cache.find(target_module_name);
        if (cache_it != module_cache.end()) {
            return cache_it->second;
        }

        std::set<std::string> modules_that_reference_target;
        std::string target_full_path = module_name + "." + target_module_name;

        for (const auto& mod : all_modules) {
            if (mod == target_module_name) continue;

            std::string mod_full_path = module_name + "." + mod;

            // Query broker for this module's parameters
            auto mod_params = m_broker.get_unconsumed_preset_values(
                [&mod_full_path](const std::pair<std::string, cci::cci_value>& iv) {
                    return iv.first.find(mod_full_path + ".") == 0;
                });

            for (const auto& param : mod_params) {
                const std::string& param_name = param.first;
                const cci::cci_value& param_value = param.second;

                if (param_name.length() > 5 && param_name.substr(param_name.length() - 5) == ".bind") {
                    if (param_value.is_string()) {
                        std::string bind_target = param_value.get_string();
                        if (bind_target.length() > 0 && bind_target[0] == '&') {
                            std::string ref_path = bind_target.substr(1);
                            if (ref_path == target_full_path ||
                                (ref_path.length() > target_full_path.length() &&
                                 ref_path.substr(0, target_full_path.length() + 1) == target_full_path + ".")) {
                                modules_that_reference_target.insert(mod);
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Cache the result
        module_cache[target_module_name] = modules_that_reference_target;
        return modules_that_reference_target;
    }

    int get_construction_priority(const std::string& name)
    {
        std::string module_name = std::string(sc_module::name());
        std::string priority_param = module_name + "." + name + ".construction_priority";

        if (m_broker.has_preset_value(priority_param)) {
            cci::cci_value cci_priority = m_broker.get_preset_cci_value(priority_param);
            if (cci_priority.is_int()) {
                return cci_priority.get_int();
            } else if (cci_priority.is_int64()) {
                return static_cast<int>(cci_priority.get_int64());
            }
        }
        return 0; // Default priority
    }

    std::list<std::string> PriorityConstruct(void)
    {
        std::list<std::string> args;
        std::set<std::string> done;
        std::list<std::string> all_modules;
        std::string module_name = std::string(sc_module::name());

        all_modules = gs::sc_cci_children(name());

        for (auto it = all_modules.begin(); it != all_modules.end();) {
            std::string mod_type_name = module_name + "." + *it + ".moduletype";
            if (!m_broker.has_preset_value(mod_type_name)) {
                it = all_modules.erase(it);
            } else {
                ++it;
            }
        }

        std::map<int, std::list<std::string>> priority_groups;
        for (const auto& mod : all_modules) {
            int priority = get_construction_priority(mod);
            priority_groups[priority].push_back(mod);
        }

        for (auto& priority_pair : priority_groups) {
            std::list<std::string> todo = priority_pair.second;
            int max_passes = todo.size() + 1;
            int pass = 0;

            bool enforce_bind_deps = true;
            while (todo.size() > 0 && pass < max_passes) {
                pass++;
                int constructed_this_pass = 0;

                m_per_pass_nested_deps_cache.clear();

                std::map<std::string, std::set<std::string>> depends_on_me;
                std::map<std::string, std::set<std::string>> bind_reverse_index;

                for (const auto& source_mod : todo) {
                    if (is_container_type(source_mod)) {
                        continue;
                    }

                    std::string source_full_path = module_name + "." + source_mod;

                    // Check if source module is a router
                    std::string source_modtype;
                    auto source_type_param = m_broker.get_preset_cci_value(source_full_path + ".moduletype");
                    if (source_type_param.is_string()) {
                        source_modtype = source_type_param.get_string();
                    }
                    bool source_is_router = (source_modtype.find("router") != std::string::npos ||
                                             source_modtype.find("Router") != std::string::npos);

                    auto mod_params = m_broker.get_unconsumed_preset_values(
                        [&source_full_path](const std::pair<std::string, cci::cci_value>& iv) {
                            return iv.first.find(source_full_path + ".") == 0;
                        });

                    for (const auto& param : mod_params) {
                        const std::string& param_name = param.first;
                        const cci::cci_value& param_value = param.second;

                        if (param_name.length() > 5 && param_name.substr(param_name.length() - 5) == ".bind") {
                            if (param_value.is_string()) {
                                std::string bind_target = param_value.get_string();
                                if (bind_target.length() > 0 && bind_target[0] == '&') {
                                    std::string ref_path = bind_target.substr(1);
                                    std::string target_mod;

                                    if (ref_path.find(module_name + ".") == 0) {
                                        target_mod = ref_path.substr(module_name.length() + 1);
                                    } else {
                                        target_mod = ref_path;
                                    }

                                    size_t dot_pos = target_mod.find('.');
                                    if (dot_pos != std::string::npos) {
                                        target_mod = target_mod.substr(0, dot_pos);
                                    }

                                    // Only track bind dependencies for router modules
                                    if (source_is_router &&
                                        std::find(todo.begin(), todo.end(), target_mod) != todo.end()) {
                                        bind_reverse_index[target_mod].insert(source_mod);
                                    }
                                }
                            }
                        }
                    }
                }

                for (const auto& mod : todo) {
                    std::set<std::string> all_deps;
                    cci::cci_value_list mod_args = get_module_args(module_name + "." + mod);
                    for (auto arg : mod_args) {
                        if (arg.is_string()) {
                            std::string a = arg.get_string();
                            if (a.length() > 0 && a[0] == '&') {
                                std::string ref_path = a.substr(1);
                                if (!ref_path.empty()) {
                                    all_deps.insert(ref_path);
                                }
                            }
                        }
                    }

                    if (is_container_type(mod)) {
                        get_nested_dependencies(mod, all_deps);
                    }

                    for (const auto& ref_path : all_deps) {
                        if (ref_path.substr(0, module_name.size()) == module_name &&
                            ref_path.length() > module_name.size() && ref_path[module_name.size()] == '.') {
                            std::string sibling_name = ref_path.substr(module_name.size() + 1);
                            size_t dot_pos = sibling_name.find('.');
                            if (dot_pos != std::string::npos) {
                                sibling_name = sibling_name.substr(0, dot_pos);
                            }
                            if (std::find(todo.begin(), todo.end(), sibling_name) != todo.end()) {
                                depends_on_me[sibling_name].insert(mod);
                            }
                        }
                    }

                    auto bind_it = bind_reverse_index.find(mod);
                    if (bind_it != bind_reverse_index.end()) {
                        for (const auto& bind_source : bind_it->second) {
                            depends_on_me[mod].insert(bind_source);
                        }
                    }
                }

                for (auto it = todo.begin(); it != todo.end();) {
                    std::string name = *it;

                    bool deps_available = are_dependencies_available(name, done);

                    bool bind_deps_ready = true;
                    if (deps_available && enforce_bind_deps) {
                        for (const auto& dep_pair : depends_on_me) {
                            const std::string& required_module = dep_pair.first;
                            const std::set<std::string>& modules_depending_on_it = dep_pair.second;

                            if (modules_depending_on_it.count(name) > 0) {
                                if (done.count(required_module) == 0) {
                                    bind_deps_ready = false;
                                    break;
                                }
                            }
                        }
                    }

                    if (deps_available && bind_deps_ready) {
                        done.insert(name);
                        it = todo.erase(it);
                        constructed_this_pass++;
                        args.push_back(name);
                    } else {
                        ++it;
                    }
                }

                if (constructed_this_pass == 0 && todo.size() > 0) {
                    if (enforce_bind_deps) {
                        enforce_bind_deps = false;
                        pass--;
                        continue;
                    }
                    SCP_WARN(())("No progress in dependency resolution, {} modules remaining", todo.size());
                    for (auto t : todo) {
                        SCP_WARN(())("Module with unresolved dependencies: {}", t);
                        std::set<std::string> all_deps;
                        cci::cci_value_list mod_args = get_module_args(module_name + "." + t);
                        for (auto arg : mod_args) {
                            if (arg.is_string()) {
                                std::string a = arg.get_string();
                                if (a.length() > 0 && a[0] == '&') {
                                    std::string ref_path = a.substr(1);
                                    if (!ref_path.empty()) {
                                        all_deps.insert(ref_path);
                                    }
                                }
                            }
                        }
                        if (is_container_type(t)) {
                            get_nested_dependencies(t, all_deps);
                        }
                        for (const auto& ref_path : all_deps) {
                            sc_core::sc_object* dep_obj = find_sc_obj(nullptr, ref_path, true);
                            if (dep_obj == nullptr) {
                                SCP_WARN(())("  Missing dependency: {}", ref_path);
                            }
                        }
                    }
                    SCP_FATAL(())("Unresolvable module dependencies");
                }
            }
        }

        return args;
    }

    void register_module_from_dylib(sc_core::sc_module_name name)
    {
        std::string libname;
        std::vector<std::string> lib_types = { ".moduletype", ".dylib_path" };

        for (const auto& type : lib_types) {
            const std::string base_name = std::string(sc_module::name()) + "." + std::string(name) + type;
            if (m_broker.has_preset_value(base_name)) {
                libname = std::string(m_broker.get_preset_cci_value(base_name).get_string()) + "." +
                          std::string(m_library_loader.get_lib_ext());
            }
        }

        qemu::LibraryLoaderIface::LibraryIfacePtr libraryHandle = m_library_loader.load_library(libname);
        if (!libraryHandle) {
            SCP_FATAL(())("Failed to load library {}: {}", libname, m_library_loader.get_last_error());
        }

        m_dls.insert(libraryHandle);

        void (*module_register)() = nullptr;
        if (libraryHandle->symbol_exists("module_register")) {
            module_register = reinterpret_cast<void (*)()>(libraryHandle->get_symbol("module_register"));
            module_register();
        } else {
            SCP_WARN(()) << "module_register not found in library " << libname;
        }
    }

    void ModulesConstruct(void)
    {
        for (auto name : PriorityConstruct()) {
            auto mod_type_name = std::string(sc_module::name()) + "." + name + ".moduletype";
            if (m_broker.has_preset_value(mod_type_name)) {
                if (m_broker.has_preset_value(std::string(sc_module::name()) + "." + name + ".dont_construct")) {
                    sc_core::sc_object* no_construct_mod_obj = nullptr;
                    std::string no_construct_mod_name = std::string(sc_module::name()) + "." + name;
                    no_construct_mod_obj = gs::find_sc_obj(nullptr, no_construct_mod_name);
                    auto no_construct_mod = dynamic_cast<sc_core::sc_module*>(no_construct_mod_obj);
                    if (!no_construct_mod) {
                        SCP_FATAL(()) << "The object " << std::string(sc_module::name()) + "." + name
                                      << " is not yet constructed";
                    }
                    m_allModules.push_back(no_construct_mod);
                } else {
                    gs::cci_clear_unused(m_broker, mod_type_name);
                    cci::cci_value cci_type = gs::cci_get(m_broker, mod_type_name);
                    if (cci_type.is_string()) {
                        std::string mod_type = cci_type.get_string();
                        cci::cci_value_list mod_args = get_module_args(std::string(sc_module::name()) + "." + name);
                        sc_core::sc_module* m = construct_module(mod_type, name.c_str(), mod_args);
                        if (!m) {
                            SC_REPORT_ERROR(
                                "ModuleFactory",
                                ("Can't automatically handle argument list for module: " + mod_type).c_str());
                        }

                        m_allModules.push_back(m);
                        if (!m_local_pass) {
                            m_local_pass = dynamic_cast<transaction_forwarder_if<PASS>*>(m);
                        }
                    } else {
                        SCP_WARN(()) << "moduletype parameter is not a string for " << name;
                    }
                }
            }
        }

        for (auto mod : m_allModules) {
            name_bind(mod);
        }
        SCP_DEBUG(())("ModulesConstruct complete");
    }

    using tlm_initiator_socket_type = tlm_utils::simple_initiator_socket_b<
        ContainerBase, DEFAULT_TLM_BUSWIDTH, tlm::tlm_base_protocol_types, sc_core::SC_ZERO_OR_MORE_BOUND>;
    using tlm_target_socket_type = tlm_utils::simple_target_socket_tagged_b<
        ContainerBase, DEFAULT_TLM_BUSWIDTH, tlm::tlm_base_protocol_types, sc_core::SC_ZERO_OR_MORE_BOUND>;

private:
    bool m_defer_modules_construct;
    std::list<sc_core::sc_module*> m_allModules;
    std::list<std::shared_ptr<sc_core::sc_module>> m_constructedModules;

    std::set<qemu::LibraryLoaderIface::LibraryIfacePtr> m_dls;
    qemu::LibraryLoaderIface& m_library_loader = qemu::get_default_lib_loader();

public:
    cci::cci_broker_handle m_broker;
    cci::cci_param<std::string> moduletype; // for consistency
    cci::cci_param<uint32_t> p_tlm_initiator_ports_num;
    cci::cci_param<uint32_t> p_tlm_target_ports_num;
    cci::cci_param<uint32_t> p_initiator_signals_num;
    cci::cci_param<uint32_t> p_target_signals_num;
    sc_core::sc_vector<tlm_initiator_socket_type> initiator_sockets;
    sc_core::sc_vector<tlm_target_socket_type> target_sockets;
    sc_core::sc_vector<InitiatorSignalSocket<bool>> initiator_signal_sockets;
    sc_core::sc_vector<TargetSignalSocket<bool>> target_signal_sockets;
    TargetSignalSocket<bool> container_self_reset;
    transaction_forwarder_if<PASS>* m_local_pass;

    std::vector<cci::cci_param<gs::cci_constructor_vl>*> registered_mods;
    sc_core::sc_object* container_mod_arg;

    virtual ~ContainerBase()
    {
        m_constructedModules.reverse();
        m_constructedModules.clear();
    }

    std::shared_ptr<sc_core::sc_module> find_module_by_name(const std::string& mod_name)
    {
        auto ret = std::find_if(m_constructedModules.begin(), m_constructedModules.end(),
                                [&](auto mod) { return (mod && std::string(mod->name()) == mod_name); });
        if (ret == m_constructedModules.end()) {
            return nullptr;
        }
        return *ret;
    }

    template <typename T>
    std::shared_ptr<sc_core::sc_module> find_module_by_cci_param(const std::string& cci_param_name, const T& cmp_value)
    {
        T ret_val;
        auto ret = std::find_if(m_constructedModules.begin(), m_constructedModules.end(), [&](auto mod) {
            return (mod && m_broker.has_preset_value(std::string(mod->name()) + "." + cci_param_name) &&
                    m_broker.get_preset_cci_value(std::string(mod->name()) + "." + cci_param_name)
                        .template try_get<T>(ret_val) &&
                    (ret_val == cmp_value));
        });
        if (ret == m_constructedModules.end()) {
            return nullptr;
        }
        return *ret;
    }

    /**
     * @brief construct a Container, and all modules within it, and perform binding
     *
     * @param _n name to give the container
     */

    ContainerBase(const sc_core::sc_module_name _n, bool defer_modules_construct,
                  sc_core::sc_object* p_container_mod_arg = nullptr)
        : m_defer_modules_construct(defer_modules_construct)
        , m_broker(cci::cci_get_broker())
        , moduletype("moduletype", "", "Module type for the TLM container, must be \"Container\"")
        , p_tlm_initiator_ports_num("tlm_initiator_ports_num", 0, "number of tlm initiator ports")
        , p_tlm_target_ports_num("tlm_target_ports_num", 0, "number of tlm target ports")
        , p_initiator_signals_num("initiator_signals_num", 0, "number of initiator signals")
        , p_target_signals_num("target_signals_num", 0, "number of target signals")
        , initiator_sockets("initiator_socket")
        , target_sockets("target_socket")
        , initiator_signal_sockets("initiator_signal_socket")
        , target_signal_sockets("target_signal_socket")
        , container_self_reset("container_self_reset")
        , m_local_pass(nullptr)
        , container_mod_arg(p_container_mod_arg)
    {
        SCP_DEBUG(()) << "ContainerBase Constructor";

        initiator_sockets.init(p_tlm_initiator_ports_num.get_value(),
                               [this](const char* n, int i) { return new tlm_initiator_socket_type(n); });
        target_sockets.init(p_tlm_target_ports_num.get_value(),
                            [this](const char* n, int i) { return new tlm_target_socket_type(n); });
        initiator_signal_sockets.init(p_initiator_signals_num.get_value(),
                                      [this](const char* n, int i) { return new InitiatorSignalSocket<bool>(n); });
        target_signal_sockets.init(p_target_signals_num.get_value(),
                                   [this](const char* n, int i) { return new TargetSignalSocket<bool>(n); });

        for (int i = 0; i < p_tlm_target_ports_num.get_value(); i++) {
            target_sockets[i].register_b_transport(this, &ContainerBase::b_transport, i);
            target_sockets[i].register_transport_dbg(this, &ContainerBase::transport_dbg, i);
            target_sockets[i].register_get_direct_mem_ptr(this, &ContainerBase::get_direct_mem_ptr, i);
        }

        for (int i = 0; i < p_tlm_initiator_ports_num.get_value(); i++) {
            initiator_sockets[i].register_invalidate_direct_mem_ptr(this, &ContainerBase::invalidate_direct_mem_ptr);
        }

        for (int i = 0; i < p_target_signals_num.get_value(); i++) {
            target_signal_sockets[i].register_value_changed_cb([&, i](bool value) {
                if (m_local_pass) m_local_pass->fw_handle_signal(i, value);
            });
        }

        container_self_reset.register_value_changed_cb([this](bool value) { do_reset(value); });

        auto mods = gs::ModuleFactory::GetAvailableModuleList();

        while (mods->size()) {
            registered_mods.push_back((mods->back())());
            mods->pop_back();
        }

        if (!m_defer_modules_construct) ModulesConstruct();
    }

    void fw_b_transport(int id, tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) override
    {
        SCP_DEBUG(()) << "calling b_transport on initiator_socket_" << id << " " << scp::scp_txn_tostring(trans);
        initiator_sockets[id]->b_transport(trans, delay);
        SCP_DEBUG(()) << "return from b_transport on initiator_socket_" << id << " " << scp::scp_txn_tostring(trans);
    }

    unsigned int fw_transport_dbg(int id, tlm::tlm_generic_payload& trans) override
    {
        SCP_DEBUG(()) << "calling transport_dbg on initiator_socket_" << id << " " << scp::scp_txn_tostring(trans);
        unsigned int ret = initiator_sockets[id]->transport_dbg(trans);
        SCP_DEBUG(()) << "return from transport_dbg on initiator_socket_" << id << " " << scp::scp_txn_tostring(trans);
        return ret;
    }

    bool fw_get_direct_mem_ptr(int id, tlm::tlm_generic_payload& trans, tlm::tlm_dmi& dmi_data) override
    {
        SCP_DEBUG(()) << "calling get_direct_mem_ptr on initiator_socket_" << id << " " << scp::scp_txn_tostring(trans);
        bool ret = initiator_sockets[id]->get_direct_mem_ptr(trans, dmi_data);
        SCP_DEBUG(()) << "return from get_direct_mem_ptr on initiator_socket_" << id << " "
                      << " RET: " << std::boolalpha << ret << " " << scp::scp_txn_tostring(trans)
                      << " IS_READ_ALLOWED: " << std::boolalpha << dmi_data.is_read_allowed() << " "
                      << " IS_WRITE_ALLOWED: " << std::boolalpha << dmi_data.is_write_allowed();
        return ret;
    }

    void fw_invalidate_direct_mem_ptr(sc_dt::uint64 start, sc_dt::uint64 end) override
    {
        SCP_DEBUG(()) << " " << name() << " invalidate_direct_mem_ptr " << " start address 0x" << std::hex << start
                      << " end address 0x" << std::hex << end;
        for (int i = 0; i < target_sockets.size(); i++) {
            target_sockets[i]->invalidate_direct_mem_ptr(start, end);
        }
    }

    void fw_handle_signal(int id, bool value) override
    {
        SCP_DEBUG(()) << "calling handle_signal on initiator_signal_socket_" << id << " value: " << std::boolalpha
                      << value;
        initiator_signal_sockets[id]->write(value);
    }

private:
    void b_transport(int id, tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        if (m_local_pass) {
            m_local_pass->fw_b_transport(id, trans, delay);
        }
    }

    unsigned int transport_dbg(int id, tlm::tlm_generic_payload& trans)
    {
        if (m_local_pass) {
            return m_local_pass->fw_transport_dbg(id, trans);
        }
        return 0;
    }

    bool get_direct_mem_ptr(int id, tlm::tlm_generic_payload& trans, tlm::tlm_dmi& dmi_data)
    {
        if (m_local_pass) {
            return m_local_pass->fw_get_direct_mem_ptr(id, trans, dmi_data);
        }
        return false;
    }

    void invalidate_direct_mem_ptr(sc_dt::uint64 start, sc_dt::uint64 end)
    {
        if (m_local_pass) {
            m_local_pass->fw_invalidate_direct_mem_ptr(start, end);
        }
    }

    std::vector<std::string> parse_order_str(const std::string& reset_order)
    {
        std::vector<std::string> ret;
        if (reset_order.empty()) {
            return ret;
        }
        std::string reset_order_tr = reset_order;
        reset_order_tr.erase(
            std::remove_if(reset_order_tr.begin(), reset_order_tr.end(), [](char c) { return std::isspace(c); }),
            reset_order_tr.end());
        size_t sig_count = std::count_if(reset_order_tr.begin(), reset_order_tr.end(), [](char c) { return c == ';'; });
        ret.resize(sig_count + 1);
        const std::regex separator(R"(;)");
        std::copy(std::sregex_token_iterator(reset_order_tr.begin(), reset_order_tr.end(), separator, -1),
                  std::sregex_token_iterator(), ret.begin());
        return ret;
    }

    void do_binding(sc_core::sc_module* module_obj, const std::string& initiator_name, const std::string& target_name)
    {
        sc_core::sc_object* i_obj = nullptr;
        sc_core::sc_object* t_obj = nullptr;
        /* find the initiator object */

        i_obj = find_sc_obj(nullptr, initiator_name);

        /* find the target object which must be inside this object */
        t_obj = find_sc_obj(module_obj, target_name);

        // actually you could probably just cast everything to (tlm::tlm_target_socket<>*), but
        // we will dynamic cast to be sure.
        if (!try_bind_all<32>(i_obj, t_obj) && !try_bind_all<64>(i_obj, t_obj)) {
            SCP_FATAL(())("No bind found for: {} to {}", i_obj->name(), t_obj->name());
        }
    }

    void order_bind(sc_core::sc_module* module_obj, const std::string& target_order_str,
                    const std::string& initiator_name)
    {
        if (!target_order_str.empty()) {
            std::vector<std::string> target_vec = parse_order_str(target_order_str);
            for (const auto& target : target_vec) {
                std::string target_name = target;
                if (target_name.at(0) != '&') continue;
                target_name.erase(0, 1);
                if (!is_absolute_path(target_name)) {
                    target_name = std::string(name()) + "." + target_name;
                }
                SCP_TRACE(())("Bind {} to {}", target_name, initiator_name);
                do_binding(module_obj, target_name, initiator_name);
            }
        }
    }

    virtual void do_reset(bool value)
    {
        if (value) SCP_WARN(()) << "Reset";
    }
};

/**
 * ModulesConstruct() function is called at Container constructor.
 */
class Container : public ContainerBase
{
public:
    SCP_LOGGER(());
    Container(const sc_core::sc_module_name& n): ContainerBase(n, false, nullptr)
    {
        SCP_DEBUG(()) << "Container constructor";
        std::string moduletype_param = std::string(name()) + ".moduletype";
        if (!m_broker.has_preset_value(moduletype_param)) {
            m_broker.set_preset_cci_value(moduletype_param, cci::cci_value("Container"));
        }
    }

    virtual ~Container() = default;
};
/**
 * ModulesConstruct() function is called at Container constructor.
 * p_container_mod_arg has to be passed as a constructor arg.
 */
class ContainerWithArgs : public ContainerBase
{
public:
    SCP_LOGGER(());
    ContainerWithArgs(const sc_core::sc_module_name& n, sc_core::sc_object* p_container_mod_arg)
        : ContainerBase(n, false, p_container_mod_arg)
    {
        SCP_DEBUG(()) << "ContainerWithArgs constructor";
        std::string moduletype_param = std::string(name()) + ".moduletype";
        if (!m_broker.has_preset_value(moduletype_param)) {
            m_broker.set_preset_cci_value(moduletype_param, cci::cci_value("ContainerWithArgs"));
        }
    }

    virtual ~ContainerWithArgs() = default;
};
/**
 * ModulesConstruct() function should be called explicitly by the user of the class.
 * The function is not called at ContainerDeferModulesConstruct constructor.
 */
class ContainerDeferModulesConstruct : public ContainerBase
{
public:
    SCP_LOGGER(());
    ContainerDeferModulesConstruct(const sc_core::sc_module_name& n): ContainerBase(n, true, nullptr)
    {
        SCP_DEBUG(()) << "ContainerDeferModulesConstruct constructor";
        std::string moduletype_param = std::string(name()) + ".moduletype";
        if (!m_broker.has_preset_value(moduletype_param)) {
            m_broker.set_preset_cci_value(moduletype_param, cci::cci_value("ContainerDeferModulesConstruct"));
        }
    }

    virtual ~ContainerDeferModulesConstruct() = default;
};

} // namespace ModuleFactory
} // namespace gs

typedef gs::ModuleFactory::Container Container;
typedef gs::ModuleFactory::ContainerDeferModulesConstruct ContainerDeferModulesConstruct;
typedef gs::ModuleFactory::ContainerWithArgs ContainerWithArgs;
#endif
