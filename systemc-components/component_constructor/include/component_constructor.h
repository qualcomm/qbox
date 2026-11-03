/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __GS_COMPONENT_CONSTRUCTOR__
#define __GS_COMPONENT_CONSTRUCTOR__

#include <cci_configuration>
#include <systemc>
#include <tlm>
#include <scp/report.h>
#include <module_factory_registery.h>
#include <cciutils.h>
#include <sstream>
#include <functional>
#include "fss_utils.h"
#include "fss_interfaces.h"
#include <dlfcn.h>

namespace gs {

class component_constructor : public sc_core::sc_module
{
    SCP_LOGGER();
    typedef fssBindIFHandle (*CreateNodeFnPtr)(fssString, fssConfigIfHandle);
    typedef fssRetStatus (*DestroyNodeFnPtr)(fssBindIFHandle);

public:
    component_constructor(const sc_core::sc_module_name& nm)
        : sc_core::sc_module(nm)
        , m_broker(cci::cci_get_broker())
        , p_node_exe_path("node_exe_path", "", "path of the node binary executable or .so")
        , p_node_name("node_name", "", "unique name of the spawned node")
        , events_binder("events_binder")
        , time_sync_binder("time_sync_binder")
        , data_binder("data_binder")
        , control_binder("control_binder")
    {
        SCP_TRACE(()) << "component_constructor constructor";
        if (p_node_exe_path.get_value().empty()) {
            SCP_FATAL(()) << "node_exe_path CCI parameter is not set!";
        }
        if (p_node_name.get_value().empty()) {
            SCP_FATAL(()) << "node_name CCI parameter is not set!";
        }
        m_config_handle.version = (1ULL << 32);
        m_config_handle.getConfigs = &component_constructor::get_configs;
        CreateNode = get_shared_lib_function_ptr<CreateNodeFnPtr>("fssCreateNode");
        DestroyNode = get_shared_lib_function_ptr<DestroyNodeFnPtr>("fssDestroyNode");
        create_node();
        init_binders();
    }

    ~component_constructor() {}

    static fssString get_configs(fssConfigIfHandle _config_if_handle, fssString name)
    {
        component_constructor* cmp = container_of<component_constructor, fssConfigIf>(
            _config_if_handle, &component_constructor::m_config_handle);
        return cmp->get_node_config(name);
    }

    fssBindIFHandle get_node_bind_handle() { return m_node_handle; }

private:
    void create_node()
    {
        m_node_handle = CreateNode(p_node_name.get_value().c_str(), &m_config_handle);
        if (!m_node_handle) {
            SCP_FATAL(()) << "Couldn't create node: " << p_node_name.get_value();
        }
    }

    void init_binders()
    {
        for (const auto& param : m_broker.get_unconsumed_preset_values()) {
            auto curr_param_name = param.first;
            auto curr_param_val = param.second.to_json();
            if ((curr_param_name.rfind(".name") + std::string(".name").size()) == curr_param_name.size()) {
                if (curr_param_name.find(events_binder.name()) != std::string::npos) {
                    events_binder.emplace_back(curr_param_val.c_str(), get_node_bind_handle());
                } else if (curr_param_name.find(time_sync_binder.name()) != std::string::npos) {
                    time_sync_binder.emplace_back(curr_param_val.c_str(), get_node_bind_handle());
                } else if (curr_param_name.find(data_binder.name()) != std::string::npos) {
                    data_binder.emplace_back(curr_param_val.c_str(), get_node_bind_handle());
                } else if (curr_param_name.find(control_binder.name()) != std::string::npos) {
                    control_binder.emplace_back(curr_param_val.c_str(), get_node_bind_handle());
                } else {
                    continue;
                }
            }
        }
    }

    const char* get_node_config(const std::string& _name)
    {
        std::string module_name = std::string(name());
        std::stringstream configs;
        for (const auto& param : m_broker.get_unconsumed_preset_values()) {
            auto param_name = param.first;
            auto key = param.first;
            auto value = param.second.to_json();
            std::string config_marker = module_name + ".node_configs";
            if (key.find(config_marker) == 0) {
                key = key.substr(config_marker.length() + 1);
                m_broker.ignore_unconsumed_preset_values(
                    [param_name](const std::pair<std::string, cci::cci_value>& iv) -> bool {
                        return iv.first == param_name;
                    });
                if (!_name.empty() && (_name != key)) continue;
                configs << key << "=" << value << ";";
            }
        }
        m_configs = configs.str();
        if (!_name.empty() && m_configs.empty()) {
            SCP_FATAL(()) << "Requested configuration: " << _name << " was not found!";
        }
        return m_configs.c_str();
    }

    template <typename T>
    T get_shared_lib_function_ptr(const char* function_name)
    {
        if (!m_lib_handle) {
            m_lib_handle = dlopen(p_node_exe_path.get_value().c_str(), RTLD_NOW | RTLD_GLOBAL);
        }
        if (!m_lib_handle) {
            SCP_FATAL(()) << "Failed to load: " << p_node_exe_path.get_value() << " Error: " << dlerror();
        }
        T fn = reinterpret_cast<T>(dlsym(m_lib_handle, function_name));
        if (!fn) {
            SCP_FATAL(()) << "function: " << function_name
                          << "is not found in the library: " << p_node_exe_path.get_value().c_str();
        }
        return fn;
    }

    void end_of_simulation() { DestroyNode(m_node_handle); }

private:
    cci::cci_broker_handle m_broker;
    cci::cci_param<std::string> p_node_exe_path;
    cci::cci_param<std::string> p_node_name;
    void* m_lib_handle = nullptr;
    fssBindIFHandle m_node_handle;
    fssConfigIf m_config_handle;
    std::string m_configs;
    CreateNodeFnPtr CreateNode;
    DestroyNodeFnPtr DestroyNode;

public:
    sc_core::sc_vector<fss_events_binder> events_binder;
    sc_core::sc_vector<fss_time_sync_binder> time_sync_binder;
    sc_core::sc_vector<fss_data_binder> data_binder;
    sc_core::sc_vector<fss_control_binder> control_binder;
};
} // namespace gs
extern "C" void module_register();
#endif
