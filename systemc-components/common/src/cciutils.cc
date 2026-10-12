/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cciutils.h>

using namespace cci;

/**
 * @brief Helper function to find a sc_object by fully qualified name
 *  throws SC_ERROR if nothing found.
 * @param m     current parent object (use null to start from the top)
 * @param name  name being searched for
 * @return sc_core::sc_object* return object if found
 */
sc_core::sc_object* gs::find_sc_obj(sc_core::sc_object* m, std::string name, bool test)
{
    if (m && name == m->name()) {
        return m;
    }

    std::vector<sc_core::sc_object*> children;
    if (m) {
        children = m->get_child_objects();
    } else {
        children = sc_core::sc_get_top_level_objects();
    }

    auto ip_it = std::find_if(begin(children), end(children), [&name](sc_core::sc_object* o) {
        return (name.compare(0, strlen(o->name()), o->name()) == 0) && find_sc_obj(o, name, true);
    });
    if (ip_it != end(children)) {
        return find_sc_obj(*ip_it, name);
    } else {
        if (!test) {
            if (m) {
                SCP_ERR("cciutils.find_sc_obj") << "Unable to find " << name << " in sc_object " << m->name();
            } else {
                SCP_ERR("cciutils.find_sc_obj") << "Unable to find " << name;
            }
        }
    }
    return nullptr;
}

/**
 * @brief return the leaf name of the given systemc hierarchical name
 *
 * @param name
 * @return std::string
 */
std::string gs::sc_cci_leaf_name(std::string name) { return name.substr(name.find_last_of(".") + 1); }

/**
 * @brief return a list of 'unconsumed' children from the given module name, can be used inside
 * or outside the heirarchy
 *
 * @param name
 * @return std::list<std::string>
 */
std::list<std::string> gs::sc_cci_children(sc_core::sc_module_name name)
{
    cci_broker_handle m_broker = (sc_core::sc_get_current_object())
                                     ? cci_get_broker()
                                     : cci_get_global_broker(cci_originator("gs__sc_cci_children"));
    std::list<std::string> children;
    int l = strlen(name) + 1;
    auto uncon = m_broker.get_unconsumed_preset_values(
        [&name](const std::pair<std::string, cci_value>& iv) { return iv.first.find(std::string(name) + ".") == 0; });
    for (auto p : uncon) {
        children.push_back(p.first.substr(l, p.first.find(".", l) - l));
    }
    children.sort();
    children.unique();
    return children;
}

// #pragma GCC diagnostic ignored "-Wunused-function"
/**
 * @brief return a list of 'unconsumed' items in the module matching the base name given, plus '_'
 * and a numeric value typically this would be a list of items from an sc_vector Can be used inside
 * or outside the heirarchy
 *
 * @param module_name : name of the module
 * @param list_name : base name of the list (e.g. name of the sc_vector)
 * @return std::list<std::string>
 */
std::list<std::string> gs::sc_cci_list_items(sc_core::sc_module_name module_name, std::string list_name)
{
    cci_broker_handle m_broker = (sc_core::sc_get_current_object())
                                     ? cci_get_broker()
                                     : cci_get_global_broker(cci_originator("gs__sc_cci_children"));
    std::string name = std::string(module_name) + "." + list_name;
    std::regex search(name + "_[0-9]+(\\.|$)");
    std::list<std::string> children;
    int l = strlen(module_name) + 1;
    auto uncon = m_broker.get_unconsumed_preset_values(
        [&search](const std::pair<std::string, cci::cci_value>& iv) { return std::regex_search(iv.first, search); });
    for (auto p : uncon) {
        std::smatch match;
        std::regex_search(p.first, match, search);
        children.push_back(p.first.substr(l, (match.length() - l) - (match.str().back() == '.' ? 1 : 0)));
    }
    children.sort();
    children.unique();
    return children;
}

std::string gs::get_parent_name(sc_core::sc_module_name n)
{
    std::string name(n);
    auto pos = name.find_last_of('.');
    if (pos != std::string::npos) {
        return name.substr(0, pos);
    }
    return "";
}

cci::cci_value gs::cci_get(cci::cci_broker_handle broker, std::string name)
{
    auto h = broker.get_param_handle(name);
    cci_clear_unused(broker, name);
    if (h.is_valid()) {
        return h.get_cci_value();
    } else {
        broker.lock_preset_value(name);
        return broker.get_preset_cci_value(name);
    }
}

void gs::cci_clear_unused(cci::cci_broker_handle broker, std::string name)
{
    HelpSingleton::GetInstance()->clear_unused(name);
}

/* handle static list for CCI parameter pre-registration */
#include <module_factory_registery.h>

std::vector<std::function<cci::cci_param<gs::cci_constructor_vl>*()>>* gs::ModuleFactory::GetAvailableModuleList()
{
    static std::vector<std::function<cci::cci_param<gs::cci_constructor_vl>*()>> list;
    // nosemgrep: reusable-workflows.semgrep_rules.cxx.locret.ret.local-address-returned -- staticlifetime
    return &list;
}

gs::HelpSingleton* gs::HelpSingleton::pHelpSingleton = NULL;

gs::HelpSingleton::HelpSingleton(): sc_core::sc_module(sc_core::sc_module_name("help_singleton")) {}

gs::HelpSingleton* gs::HelpSingleton::GetInstance()
{
    if (pHelpSingleton == NULL) {
        pHelpSingleton = new HelpSingleton();
    }
    return pHelpSingleton;
}