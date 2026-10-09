/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "container_builder.h"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>

namespace gs {

container_builder::container_builder(sc_core::sc_module_name _name)
    : gs::ModuleFactory::ContainerDeferModulesConstruct(_name)
{
    dispatch(std::string(this->name()));
    redirect_socket_params(std::string(this->name()));
    ModulesConstruct();
}

void container_builder::dispatch(const std::string& _name)
{
    if (_name.empty()) {
        SCP_FATAL(()) << "dispatch: function was called with empty string!";
    }

    std::string config_table_prefix;

    auto all_config_params = m_broker.get_unconsumed_preset_values(
        [&_name](const std::pair<std::string, cci_value>& iv) {
            return iv.first.find(_name + ".config.") != std::string::npos;
        });

    for (const auto& param : all_config_params) {
        if (param.first.find(".config.") != std::string::npos) {
            size_t config_pos = param.first.find(".config.");
            config_table_prefix = param.first.substr(0, config_pos + std::string(".config").size());
            break;
        }
    }

    if (config_table_prefix.empty()) {
        SCP_WARN(()) << "No config table found for container_builder" << _name;
        return;
    }

    std::vector<std::string> params_to_ignore;

    for (const auto& param : all_config_params) {
        std::string old_name = param.first;
        cci_value param_value = param.second;

        if (old_name.find(".sockets.") != std::string::npos) {
            size_t sockets_pos = old_name.find(".sockets.");
            std::string socket_name = old_name.substr(sockets_pos + std::string(".sockets.").size());

            if (param_value.is_string()) {
                std::string internal_path = param_value.get_string();
                if (internal_path.length() > 0 && internal_path[0] == '&') {
                    internal_path = internal_path.substr(1);
                    m_socket_redirects[socket_name] = internal_path;
                    params_to_ignore.push_back(old_name);
                    continue;
                }
            }
        }

        std::string new_name = old_name;
        size_t config_pos = old_name.find(config_table_prefix);
        if (config_pos == 0) {
            new_name = _name + old_name.substr(config_table_prefix.length());
            m_broker.set_preset_cci_value(new_name, param_value);
            params_to_ignore.push_back(old_name);
        }
    }

    for (const auto& old_name : params_to_ignore) {
        m_broker.lock_preset_value(old_name);
    }

    m_broker.ignore_unconsumed_preset_values([&config_table_prefix](const std::pair<std::string, cci_value>& iv) {
        return iv.first.find(config_table_prefix) == 0;
    });
}

void container_builder::redirect_socket_params(const std::string& _name)
{
    if (_name.empty()) {
        SCP_FATAL(()) << "redirect_socket_params: function was called with empty string!";
    }

    const std::string redirect_cycle = get_socket_redirect_cycle(m_socket_redirects);
    if (!redirect_cycle.empty()) {
        SCP_FATAL(()) << "Cyclic socket redirects in " << _name << ": " << redirect_cycle;
        return;
    }

    std::map<std::string, size_t> redirect_source_count;
    for (const auto& redirect : m_socket_redirects) {
        redirect_source_count.emplace(redirect.first, 0);
    }
    for (const auto& redirect : m_socket_redirects) {
        const std::string target_alias = redirect.second.substr(0, redirect.second.find('.'));
        const auto target = redirect_source_count.find(target_alias);
        if (target != redirect_source_count.end()) {
            ++target->second;
        }
    }

    std::set<std::string> ready_aliases;
    for (const auto& alias : redirect_source_count) {
        if (alias.second == 0) {
            ready_aliases.insert(alias.first);
        }
    }

    std::vector<std::string> redirect_order;
    while (!ready_aliases.empty()) {
        const std::string alias_name = *ready_aliases.begin();
        ready_aliases.erase(ready_aliases.begin());
        redirect_order.emplace_back(alias_name);

        const std::string& internal_path = m_socket_redirects.at(alias_name);
        const std::string target_alias = internal_path.substr(0, internal_path.find('.'));
        const auto target = redirect_source_count.find(target_alias);
        if (target != redirect_source_count.end() && --target->second == 0) {
            ready_aliases.insert(target->first);
        }
    }

    using cci_name_value = std::pair<std::string, cci_value>;
    const std::string container_prefix = _name + ".";
    std::map<std::string, std::vector<cci_name_value>> alias_params_by_name;
    std::map<std::string, std::map<std::string, size_t>> alias_param_indices_by_name;
    std::unordered_map<std::string, std::vector<size_t>> bind_param_indices_by_value;
    std::vector<size_t> compound_bind_param_indices;

    auto replace_bind_param_index = [&](const std::string& old_bind_path, std::string new_bind_path,
                                        size_t param_index) {
        auto old_index = bind_param_indices_by_value.find(old_bind_path);
        if (old_index != bind_param_indices_by_value.end()) {
            auto& indices = old_index->second;
            indices.erase(std::remove(indices.begin(), indices.end(), param_index), indices.end());
            if (indices.empty()) {
                bind_param_indices_by_value.erase(old_index);
            }
        }
        bind_param_indices_by_value[std::move(new_bind_path)].push_back(param_index);
    };

    auto cache_alias_param = [&](const cci_name_value& param, const std::string* current_alias = nullptr) {
        if (param.first.find(container_prefix) != 0) {
            return;
        }
        const std::string relative_name = param.first.substr(container_prefix.length());
        const size_t dot_pos = relative_name.find('.');
        if (dot_pos == std::string::npos) {
            return;
        }
        const std::string alias_name = relative_name.substr(0, dot_pos);
        if (current_alias && alias_name == *current_alias) {
            return;
        }
        if (m_socket_redirects.find(alias_name) == m_socket_redirects.end()) {
            return;
        }

        auto& alias_params = alias_params_by_name[alias_name];
        auto& alias_param_indices = alias_param_indices_by_name[alias_name];
        const auto [index_it, inserted] = alias_param_indices.emplace(param.first, alias_params.size());
        if (inserted) {
            alias_params.emplace_back(param);
        } else if (!(alias_params[index_it->second].second == param.second)) {
            alias_params[index_it->second].second = param.second;
        }
    };

    // Resolve bind targets before copying alias parameters to internal sockets
    // and locking them, so copied .bind values cannot retain unresolved aliases.
    auto preset_params = m_broker.get_unconsumed_preset_values();
    bind_param_indices_by_value.reserve(preset_params.size());
    for (size_t param_index = 0; param_index < preset_params.size(); ++param_index) {
        const auto& param = preset_params[param_index];
        if (param.first.find(".bind") != std::string::npos) {
            if (param.second.is_string()) {
                std::string bind_path = param.second.get_string();
                if (bind_path.find(';') == std::string::npos) {
                    bind_param_indices_by_value[std::move(bind_path)].push_back(param_index);
                } else {
                    compound_bind_param_indices.push_back(param_index);
                }
            }
        }
    }

    for (const auto& alias_name : redirect_order) {
        const std::string& internal_path = m_socket_redirects.at(alias_name);
        std::string alias_socket_path = _name + "." + alias_name;
        std::string internal_socket_path = _name + "." + internal_path;

        std::string relative_alias_path = alias_socket_path;
        std::string relative_internal_path = internal_socket_path;

        size_t first_dot = _name.find_first_of('.');
        if (first_dot != std::string::npos) {
            std::string parent_prefix = _name.substr(0, first_dot + 1);
            if (alias_socket_path.find(parent_prefix) == 0) {
                relative_alias_path = alias_socket_path.substr(parent_prefix.length());
            }
            if (internal_socket_path.find(parent_prefix) == 0) {
                relative_internal_path = internal_socket_path.substr(parent_prefix.length());
            }
        }

        std::string search_paths[] = { "&" + alias_socket_path, alias_socket_path, "&" + relative_alias_path,
                                       relative_alias_path };
        auto is_absolute_alias_path = [&alias_socket_path](const std::string& path) {
            return path == "&" + alias_socket_path || path == alias_socket_path || path.find("&platform.") == 0 ||
                   path.find("platform.") == 0;
        };
        std::vector<size_t> matching_param_indices = compound_bind_param_indices;
        for (const auto& search_path : search_paths) {
            const auto match = bind_param_indices_by_value.find(search_path);
            if (match != bind_param_indices_by_value.end()) {
                matching_param_indices.insert(matching_param_indices.end(), match->second.begin(), match->second.end());
            }
        }
        std::sort(matching_param_indices.begin(), matching_param_indices.end());
        matching_param_indices.erase(std::unique(matching_param_indices.begin(), matching_param_indices.end()),
                                     matching_param_indices.end());

        for (const size_t param_index : matching_param_indices) {
            auto& param = preset_params[param_index];
            std::string param_name = param.first;
            cci_value param_value = param.second;

            if (param_value.is_string()) {
                std::string bind_path = param_value.get_string();

                bool matched = false;
                for (const auto& search_path : search_paths) {
                    if (bind_path == search_path) {
                        matched = true;
                        break;
                    }
                }

                if (matched) {
                    std::string new_bind_path = "&" + (is_absolute_alias_path(bind_path) ? internal_socket_path
                                                                                         : relative_internal_path);

                    m_broker.set_preset_cci_value(param_name, cci_value(new_bind_path));
                    param.second = cci_value(new_bind_path);
                    replace_bind_param_index(bind_path, std::move(new_bind_path), param_index);
                } else if (bind_path.find(';') != std::string::npos) {
                    bool replaced = false;
                    std::string new_bind_path = bind_path;

                    for (const auto& search_path : search_paths) {
                        size_t pos = 0;
                        while ((pos = new_bind_path.find(search_path, pos)) != std::string::npos) {
                            bool valid_match = true;
                            if (pos + search_path.length() < new_bind_path.length()) {
                                char next_char = new_bind_path[pos + search_path.length()];
                                if (next_char != ';' && next_char != ' ' && next_char != '\t' && next_char != '\n' &&
                                    next_char != '\r') {
                                    valid_match = false;
                                }
                            }

                            if (valid_match) {
                                std::string replacement;
                                replacement = "&" + (is_absolute_alias_path(search_path) ? internal_socket_path
                                                                                         : relative_internal_path);
                                new_bind_path.replace(pos, search_path.length(), replacement);
                                replaced = true;
                                pos += replacement.length();
                            } else {
                                pos += search_path.length();
                            }
                        }
                        if (replaced) {
                            break;
                        }
                    }

                    if (replaced) {
                        m_broker.set_preset_cci_value(param_name, cci_value(new_bind_path));
                        param.second = cci_value(new_bind_path);
                    }
                }
            }
        }
    }

    for (const auto& param : preset_params) {
        cache_alias_param(param);
    }

    for (const auto& alias_name : redirect_order) {
        const std::string alias_prefix = container_prefix + alias_name;
        const std::string internal_prefix = container_prefix + m_socket_redirects.at(alias_name);

        for (const auto& param : alias_params_by_name[alias_name]) {
            const std::string internal_param_name = internal_prefix + param.first.substr(alias_prefix.length());
            m_broker.set_preset_cci_value(internal_param_name, param.second);
            cache_alias_param(std::make_pair(internal_param_name, param.second), &alias_name);
            m_broker.lock_preset_value(param.first);
        }
    }

    m_broker.ignore_unconsumed_preset_values([this, container_prefix](const std::pair<std::string, cci_value>& iv) {
        if (iv.first.find(container_prefix) != 0) {
            return false;
        }
        const std::string relative_name = iv.first.substr(container_prefix.length());
        const size_t dot_pos = relative_name.find('.');
        return dot_pos != std::string::npos &&
               m_socket_redirects.find(relative_name.substr(0, dot_pos)) != m_socket_redirects.end();
    });
}

std::string container_builder::get_socket_redirect_cycle(const std::map<std::string, std::string>& socket_redirects)
{
    std::set<std::string> completed_aliases;

    for (const auto& redirect : socket_redirects) {
        if (completed_aliases.count(redirect.first) != 0) {
            continue;
        }

        std::map<std::string, size_t> path_indices;
        std::vector<std::string> path;
        std::string current_alias = redirect.first;
        while (true) {
            const auto [path_it, inserted] = path_indices.emplace(current_alias, path.size());
            if (!inserted) {
                std::string cycle;
                for (size_t i = path_it->second; i < path.size(); ++i) {
                    if (!cycle.empty()) {
                        cycle += " -> ";
                    }
                    cycle += path[i];
                }
                return cycle + " -> " + current_alias;
            }

            path.emplace_back(current_alias);
            const auto current_redirect = socket_redirects.find(current_alias);
            const std::string& internal_path = current_redirect->second;
            const size_t target_end = internal_path.find('.');
            const std::string target_alias = internal_path.substr(0, target_end);
            if (socket_redirects.find(target_alias) == socket_redirects.end() ||
                completed_aliases.count(target_alias) != 0) {
                break;
            }
            current_alias = target_alias;
        }

        completed_aliases.insert(path.begin(), path.end());
    }

    return {};
}

std::string container_builder::replace_all(std::string str, const std::string& from, const std::string& to)
{
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
    return str;
}

} // namespace gs

typedef gs::container_builder container_builder;
void module_register() { GSC_MODULE_REGISTER_C(container_builder); }
