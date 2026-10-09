/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CCIUTILS_H
#define CCIUTILS_H

#include <scp/report.h>
#include <iostream>
#include <list>
#include <regex>
#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include "luafile_tool.h"
#include <cci_configuration>
#define SC_INCLUDE_DYNAMIC_PROCESSES
#include <systemc>
#include <tlm>
#include <tuple>

/*********************
 * CCI Convenience
 *********************/

namespace gs {
using namespace cci;

// Marks modules that construct child modules through ContainerBase.
class container_module
{
public:
    virtual ~container_module() = default;
};

/**
 * @brief Basic function container for CCI
 * Packing and unpacking are not supported
 *
 * @tparam templated over any function T(U...)
 */
template <typename, typename... U>
class cci_function; // undefined
template <typename T, typename... U>
class cci_function<T(U...)> : public std::function<T(U...)>
{
    using std::function<T(U...)>::function;

public:
    // add operater== as required by CCI
    bool operator==(const cci_function& rhs) const
    {
        assert(false);
        return false;
    }
};

/**
 * @brief sc_module constructor container for CCI
 * This uses the CCI Function container, and the FactoryMaker in order to parameterise the constructor and maintain type
 * safety The actual parameters will be extracted (in a type safe manor) from a CCI list
 */
class cci_constructor_vl : public std::function<sc_core::sc_module*(sc_core::sc_module_name, cci::cci_value_list)>
{
    using std::function<sc_core::sc_module*(sc_core::sc_module_name, cci::cci_value_list)>::function;

    /* local 'apply' helper */
    template <typename _T, typename... U, size_t... I>
    _T* new_t(sc_core::sc_module_name name, std::tuple<U...> tuple, std::index_sequence<I...>)
    {
        (void)tuple; // mark tupal as used for older compilers.
        return new _T(name, std::get<I>(tuple)...);
    }

public:
    /**
     * @brief The FactoryMaker carries the type infomation into the constructor functor.
     *
     * @tparam Templated over T(U..)
     */
    template <typename T, typename... U>
    class FactoryMaker
    {
    public:
        const char* type;
        FactoryMaker<T, U...>(const char* _t): type(_t) {}
    };

    const char* type = ""; // maintain a string representation of the type.
    bool m_is_container = false;

    bool is_container() const { return m_is_container; }

    /**
     * @brief Construct a new cci constructor vl object
     * The FactoryMaker carries the type info into this constructor
     * If the cci params can not be converted to the correct types (or the
     * number of params is incorrect), the constructor generates errors.
     * @tparam templated over T(U..)
     */
    template <typename _T, typename... U>
    cci_constructor_vl(FactoryMaker<_T, U...> fm)
        : std::function<sc_core::sc_module*(sc_core::sc_module_name name, cci::cci_value_list v)>(
              [&](sc_core::sc_module_name name, cci::cci_value_list v) -> sc_core::sc_module* {
                  if (sizeof...(U) != v.size()) {
                      SC_REPORT_ERROR("GS CCI UTILS",
                                      ("Wrong number of CCI arguments for module " + std::string(name)).c_str());
                  }
                  int n = 0;
                  try {
                      std::tuple<U...> tuple = { v[n++].get<U>()... };
                      auto is = std::make_index_sequence<sizeof...(U)>{};
                      return new_t<_T, U...>(name, tuple, is);
                  } catch (...) {
                      SC_REPORT_ERROR("GS CCI UTILS",
                                      ("CCI argument types dont match for module " + std::string(name)).c_str());
                  }
                  return nullptr;
              })
    {
        type = fm.type;
        m_is_container = std::is_base_of_v<container_module, _T>;
    }

    // add operater== as required by CCI
    bool operator==(const cci_constructor_vl& rhs) const
    {
        SCP_FATAL("cciutils.operator") << "Operator required by CCI";
        return false;
    }
};

/**
 * @brief Helper function to find a sc_object by fully qualified name
 *  throws SC_ERROR if nothing found.
 * @param m     current parent object (use null to start from the top)
 * @param name  name being searched for
 * @return sc_core::sc_object* return object if found
 */
sc_core::sc_object* find_sc_obj(sc_core::sc_object* m, std::string name, bool test = false);

/**
 * @brief Helper function to find a sc_objects of a given type
 * @param m     current parent object (use null to start from the top)
 * @tparam T typename being searched for
 * @return std::vector<sc_core::sc_object*> list of sc_objects found
 */
template <typename T>
std::vector<T*> find_sc_objects(sc_core::sc_object* m = nullptr)
{
    std::vector<T*> res;
    if (m) {
        T* o = dynamic_cast<T*>(m);
        if (o) res.push_back(o);
    }
    std::vector<sc_core::sc_object*> children;
    if (m) {
        children = m->get_child_objects();
    } else {
        children = sc_core::sc_get_top_level_objects();
    }
    for (auto o : children) {
        auto c = find_sc_objects<T>(o);
        res.insert(res.end(), std::make_move_iterator(c.begin()), std::make_move_iterator(c.end()));
    }
    return res;
}

/**
 * @brief return the leaf name of the given systemc hierarchical name
 *
 * @param name
 * @return std::string
 */
std::string sc_cci_leaf_name(std::string name);

/**
 * @brief return a list of 'unconsumed' children from the given module name, can be used inside
 * or outside the heirarchy
 *
 * @param name
 * @return std::list<std::string>
 */
std::list<std::string> sc_cci_children(sc_core::sc_module_name name);

/**
 * @brief return a list of 'unconsumed' items in the module matching the base name given, plus '_'
 * and a numeric value typically this would be a list of items from an sc_vector Can be used inside
 * or outside the heirarchy
 *
 * @param module_name : name of the module
 * @param list_name : base name of the list (e.g. name of the sc_vector)
 * @return std::list<std::string>
 */
std::list<std::string> sc_cci_list_items(sc_core::sc_module_name module_name, std::string list_name);

std::string get_parent_name(sc_core::sc_module_name n);

cci::cci_value cci_get(cci::cci_broker_handle broker, std::string name);

template <typename T>
T cci_get(cci::cci_broker_handle broker, std::string name)
{
    T ret{};
    cci_value v = cci_get(broker, name);

    if (!v.template try_get<T>(ret)) {
        SCP_ERR("cciutils.cci_get") << "Unable to get parameter " << name << "\nIs your config file up-to-date?";
    }
    return ret;
}

template <typename T>
T cci_get_d(cci::cci_broker_handle broker, std::string name, T default_val)
{
    T ret{};
    cci_value v = cci_get(broker, name);

    if (!v.template try_get<T>(ret)) {
        return default_val;
    }
    return ret;
}

template <typename T>
bool cci_get(cci::cci_broker_handle broker, std::string name, T& param)
{
    return cci_get(broker, name).try_get<T>(param);
}

class HelpSingleton : public sc_core::sc_module
{
public:
    static HelpSingleton* GetInstance();
    std::set<std::string> m_unused;
    std::set<std::string> m_used;
    std::set<std::string> m_capture;

    void start_of_simulation()
    {
        if (!m_unused.empty()) {
            SCP_WARN("Param check") << "";
            SCP_WARN("Param check") << "Unused Parameters:";
            SCP_WARN("Param check") << "===================================";
            for (auto p : m_unused) {
                SCP_WARN("Param check") << "Params: Unused parameter : " << p;
            }
        }
    }

    std::vector<std::tuple<std::string, std::string, std::string>> get_used_parameters(cci::cci_broker_handle broker,
                                                                                       std::string path = "",
                                                                                       bool print_params = true)
    {
        std::vector<std::tuple<std::string, std::string, std::string>> ret; // tuple<param_name, param_val, param_desc>
        bool found_match = false;
        for (auto v : m_used) {
            if (v.rfind(path, 0) == 0) {
                cci::cci_value c = cci_get(broker, v);
                if (c.to_json() == "null") {
                    if (print_params) std::cout << "Configurable Parameters: " << v << " the value is not set";
                } else {
                    if (print_params) std::cout << "Configurable Parameters: " << v << " the value is: " << c.to_json();
                }
                auto h = broker.get_param_handle(v);
                std::string param_desc = "null";
                if (h.is_valid()) {
                    param_desc = h.get_description();
                    if (print_params) std::cout << " (" << param_desc << ")";
                }
                if (print_params) std::cout << std::endl;
                found_match = true;
                ret.push_back(std::make_tuple(v, c.to_json(), param_desc));
            }
        }
        if (!path.empty() && !found_match) {
            SCP_FATAL("cciutils") << "No parameter matching '" << path << "'.";
        }
        return ret;
    }

    void set_unused(const std::string& parname, const cci_originator& originator)
    {
        if (m_capture.find(originator.name()) != m_capture.end()) m_unused.insert(parname);
    }

    void capture_originator(std::string name) { m_capture.insert(name); }

    void clear_unused(const std::string& parname)
    {
        m_used.insert(parname);
        auto iter = m_unused.find(parname);
        if (iter != m_unused.end()) {
            m_unused.erase(iter);
        }
    }

private:
    HelpSingleton();
    static HelpSingleton* pHelpSingleton;
};

/**
 * @brief Configurable Broker class, inherits from the 'standard' cci_utils broker, but adds
 * 1/ The ability to be instanced in the constructor
 * 2/ The ability to automatically load a configuration file
 * 3/ Explicitly set parameters (from the constructor) are 'hidden' from the parent broker
 */
class ConfigurableBroker : public cci_utils::consuming_broker
{
private:
    std::set<std::string> m_unignored; // before conf_file
    // Retain locally preset values with their names. Repeated filtered
    // enumeration otherwise performs a broker lookup for every parameter,
    // and re-exposed parameters retain their value after being consumed.
    std::unordered_map<std::string, cci_value> m_unignored_values;

public:
    // a set of perameters that should be exposed up the broker stack
    HelpSingleton* m_help_helper;
    std::set<std::string> hide;
    friend class PrivateConfigurableBroker;

    static cci_broker_if& get_broker_interface(cci_broker_handle broker) { return unwrap_broker(broker); }

    std::vector<std::string> get_unconsumed_preset_names_with_prefix(const std::string& prefix) const
    {
        std::vector<std::string> names;
        auto current = m_unignored.lower_bound(prefix);
        while (current != m_unignored.end() && current->compare(0, prefix.length(), prefix) == 0) {
            names.emplace_back(*current++);
        }
        return names;
    }

protected:
    std::string m_orig_name;
    cci_originator m_originator;
    bool has_parent;
    cci_broker_if& m_parent;
    cci_param<ConfigurableBroker*>* m_child_ref;

    std::unordered_set<std::string> m_initialized;
    std::function<void(std::string)> m_uninitialized_cb;

    friend class cci_value_converter<ConfigurableBroker>;

    // convenience function for constructor
    cci_broker_if& get_parent_broker()
    {
        if (sc_core::sc_get_current_object()) {
            has_parent = true;
            return unwrap_broker(cci_get_broker());
        } else {
            // We ARE the global broker
            has_parent = false;
            return *this;
        }
    }
    std::string hierarchical_name()
    {
        if (!sc_core::sc_get_current_object()) {
            return std::string("");
        } else {
            return cci_originator().name();
        }
    }
    cci_originator get_cci_originator(const char* n)
    {
        if (!sc_core::sc_get_current_object()) {
            return cci_originator(n);
        } else {
            return cci_originator();
        }
    }
    inline bool is_log_param(const std::string& parname) const
    {
        std::string s_SCP_LOG_LEVEL_PARAM_NAME = SCP_LOG_LEVEL_PARAM_NAME;
        int s_SCP_LOG_LEVEL_PARAM_NAME_length = s_SCP_LOG_LEVEL_PARAM_NAME.length();
        int p_len = parname.length();
        if (p_len >= s_SCP_LOG_LEVEL_PARAM_NAME_length) {
            return (parname.compare(p_len - s_SCP_LOG_LEVEL_PARAM_NAME_length, s_SCP_LOG_LEVEL_PARAM_NAME_length,
                                    s_SCP_LOG_LEVEL_PARAM_NAME) == 0);
        } else {
            return false;
        }
    }
    /**
     * @brief private function to determine if we send to the parent broker or not
     *
     */
    virtual bool sendToParent(const std::string& parname) const
    {
        if (m_uninitialized_cb && (!is_log_param(parname))) {
            while (1) {
                auto pos = std::string::npos;
                pos = parname.find_last_of('.', pos);
                if (pos == std::string::npos) break;
                auto parent = parname.substr(0, pos);
                if (m_initialized.count(parent) != 0) break;
                const_cast<ConfigurableBroker*>(this)->m_initialized.insert(parent);
                m_uninitialized_cb(parent);
            }
        }
        return ((hide.find(parname) == hide.end()) && (!is_global_broker()));
    }

    /**
     * @brief Expose all params that have not been configured
     *
     * @param list
     */
    void initialize_params(const std::initializer_list<cci_name_value_pair>& list)
    {
        using namespace cci;

        for (auto& p : list) {
            hide.insert(relname(p.first));
            if (!p.second.is_null()) {
                set_preset_cci_value(relname(p.first), p.second, m_originator);
            }
        }
    }

    void alias_params(const std::initializer_list<std::pair<std::string, std::string>>& list)
    {
        // handle aliases
        for (auto& p : list) {
            std::string aliasname = relname(p.second);
            set_preset_cci_value(relname(p.first), get_preset_cci_value(aliasname), m_originator);
            alias_param(relname(p.first), aliasname);
        }
    }
    /**
     *  @fn     void typed_post_write_callback(const cci::cci_param_write_event<int> & ev)
     *  @brief  Post Callback function to sync ongoing written parameter value with synced_handle
     * value
     *  @return void
     */
    void untyped_post_write_callback(const cci::cci_param_write_event<>& ev, cci::cci_param_handle synced_handle)
    {
        synced_handle.set_cci_value(ev.new_value);
    }
    /**
     *  @fn     void sync_values(cci::cci_param_handle _param_handle_1, cci::cci_param_handle
     * _param_handle_2)
     *  @brief  Function for synchronizing the values of cci_parameter of OWNER
     *          modules via the PARAM_VALUE_SYNC
     *  @param  _param_handle_1 The first parameter to be synced
     *  @param  _param_handle_2 The second parameter to be synced
     *  @return void
     */
    void sync_values(cci::cci_param_handle _param_handle_1, cci::cci_param_handle _param_handle_2)
    {
        // In order to synchronize even the default values of the owner modules,
        // use cci_base_param of one parameter as reference, write the same value
        // to the other pararmeter's cci_base_param using JSON
        _param_handle_1.set_cci_value(_param_handle_2.get_cci_value());

        post_write_cb_vec.push_back(_param_handle_1.register_post_write_callback(
            sc_bind(&ConfigurableBroker::untyped_post_write_callback, this, sc_unnamed::_1, _param_handle_2)));

        post_write_cb_vec.push_back(_param_handle_2.register_post_write_callback(
            sc_bind(&ConfigurableBroker::untyped_post_write_callback, this, sc_unnamed::_1, _param_handle_1)));
    }

    std::vector<cci::cci_callback_untyped_handle> post_write_cb_vec; ///< Callback Adaptor Objects

    cci_param_create_callback_handle register_cb;
    std::vector<std::pair<std::string, std::string>> alias_list;

    void alias_param(std::string a, std::string b)
    {
        alias_list.push_back(std::make_pair(a, b));
        if (register_cb == cci_param_create_callback_handle()) {
            register_cb = register_create_callback(
                sc_bind(&ConfigurableBroker::alias_param_callback, this, sc_unnamed::_1), m_originator);
        }
        alias_param_callback(cci::cci_param_handle());
    }

    void alias_param_callback(const cci_param_untyped_handle& ph)
    {
        alias_list.erase(std::remove_if(alias_list.begin(), alias_list.end(),
                                        [&](auto a) {
                                            cci_param_untyped_handle h_a = get_param_handle(a.first, m_originator);
                                            if (h_a.is_valid() && h_a.get_mutable_type() == cci::CCI_IMMUTABLE_PARAM) {
                                                return true;
                                            }
                                            cci_param_untyped_handle h_b = get_param_handle(a.second, m_originator);
                                            if (h_b.is_valid() && !h_a.is_valid()) {
                                                set_preset_cci_value(a.first, h_b.get_cci_value(), m_originator);
                                                return false; // maybe it'll be mutable when it arrives?
                                            }
                                            if (h_a.is_valid() && h_b.is_valid()) {
                                                sync_values(h_a, h_b);
                                                return true;
                                            }
                                            return false;
                                        }),
                         alias_list.end());

        if (alias_list.size() == 0) {
            unregister_create_callback(register_cb, m_originator);
        }
    }

public:
    /*
     * public interface functions
     */

    std::vector<cci_name_value_pair> get_consumed_preset_values() const
    {
        std::vector<cci_name_value_pair> consumed_preset_cci_values;
        std::map<std::string, cci_value>::const_iterator iter;
        std::vector<cci_preset_value_predicate>::const_iterator pred;

        for (iter = m_unused_value_registry.begin(); iter != m_unused_value_registry.end(); ++iter) {
            for (pred = m_ignored_unconsumed_predicates.begin(); pred != m_ignored_unconsumed_predicates.end();
                 ++pred) {
                const cci_preset_value_predicate& p = *pred; // get the actual predicate
                if (p(std::make_pair(iter->first, iter->second))) {
                    break;
                }
            }
            if (pred != m_ignored_unconsumed_predicates.end()) {
                consumed_preset_cci_values.push_back(std::make_pair(iter->first, iter->second));
            }
        }
        return consumed_preset_cci_values;
    }

/**
 * @brief Construct a new Configurable Broker object
 * default constructor:
 * When constructed with no initialised parameters, it is assumed that ALL
 * parameters are to be treated as private
 * @param name Broker name (Default provided)
 */
#define BROKERNAME "gs::ConfigurableBroker"
    ConfigurableBroker(const std::string& name = BROKERNAME,
                       std::function<void(std::string)> uninitialized_cb = nullptr)
        : consuming_broker(hierarchical_name() + "." + name)
        , m_help_helper(HelpSingleton::GetInstance())
        , m_orig_name(hierarchical_name())
        , m_originator(get_cci_originator(name.c_str()))
        , m_parent(get_parent_broker()) // local convenience function
        , m_child_ref(NULL)
        , m_uninitialized_cb(uninitialized_cb)

    {
        if (has_parent) {
            m_child_ref = new cci_param<ConfigurableBroker*>((name + ".childbroker").c_str(), (this), "");
        }

        cci_register_broker(this);
    }

    /**
     * @brief Construct a new Configurable Broker object from list.
     *  When constructed with a list of initialised parameters, all other params
     *  will be exported to the parent broker
     * @param list
     */
    ConfigurableBroker(std::initializer_list<cci_name_value_pair> list,
                       std::initializer_list<std::pair<std::string, std::string>> alias_list = {},
                       std::function<void(std::string)> uninitialized_cb = nullptr)
        : ConfigurableBroker(BROKERNAME, uninitialized_cb)
    {
        initialize_params(list);
        alias_params(alias_list);
    }

    std::string relname(const std::string& n) const
    {
        return (m_orig_name.empty()) ? n : m_orig_name + std::string(".") + n;
    }

    ~ConfigurableBroker()
    {
        if (m_child_ref) {
            delete m_child_ref;
        }
    }

    void set_uninitialized_cb(std::function<void(std::string)> uninitialized_cb)
    {
        m_uninitialized_cb = uninitialized_cb;
    }
    void clear_uninitialized_cb() { m_uninitialized_cb = nullptr; }

    cci_originator get_value_origin(const std::string& parname) const override
    {
        if (sendToParent(parname)) {
            return m_parent.get_value_origin(parname);
        } else {
            return consuming_broker::get_value_origin(parname);
        }
    }

    /* NB  missing from upstream CCI 'broker' see
     * https://github.com/OSCI-WG/cci/issues/258 */
    bool has_preset_value(const std::string& parname) const override
    {
        if (sendToParent(parname)) {
            return m_parent.has_preset_value(parname);
        } else {
            return consuming_broker::has_preset_value(parname);
        }
    }

    cci_value get_preset_cci_value(const std::string& parname) const override
    {
        if (sendToParent(parname)) {
            return m_parent.get_preset_cci_value(parname);
        } else {
            return consuming_broker::get_preset_cci_value(parname);
        }
    }

    void lock_preset_value(const std::string& parname) override
    {
        if (sendToParent(parname)) {
            return m_parent.lock_preset_value(parname);
        } else {
            return consuming_broker::lock_preset_value(parname);
        }
    }

    cci_value get_cci_value(const std::string& parname,
                            const cci::cci_originator& originator = cci::cci_originator()) const override
    {
        if (sendToParent(parname)) {
            return m_parent.get_cci_value(parname, originator);
        } else {
            return consuming_broker::get_cci_value(parname, originator);
        }
    }

    void add_param(cci_param_if* par) override
    {
        m_help_helper->clear_unused(par->name());
        if (sendToParent(par->name())) {
            return m_parent.add_param(par);
        } else {
            m_unignored.erase(par->name());
            return consuming_broker::add_param(par);
        }
    }

    void remove_param(cci_param_if* par) override
    {
        if (sendToParent(par->name())) {
            return m_parent.remove_param(par);
        } else {
            m_unignored.insert(par->name());
            return consuming_broker::remove_param(par);
        }
    }

    // Upstream consuming broker fails to pass get_unconsumed_preset_value to parent
    std::vector<cci_name_value_pair> get_unconsumed_preset_values() const override
    {
        std::vector<cci_name_value_pair> r;
        for (const auto& u : m_unignored) {
            auto value_it = m_unignored_values.find(u);
            r.emplace_back(u, value_it != m_unignored_values.end() ? value_it->second : get_preset_cci_value(u));
        }
        if (has_parent) {
            std::vector<cci_name_value_pair> p = m_parent.get_unconsumed_preset_values();
            r.insert(r.end(), p.begin(), p.end());
        }
        return r;
    }

    // Upstream consuming broker fails to pass ignore_unconsumed_preset_values
    void ignore_unconsumed_preset_values(const cci_preset_value_predicate& pred) override
    {
        if (has_parent) {
            m_parent.ignore_unconsumed_preset_values(pred);
        }
        for (auto u = m_unignored.begin(); u != m_unignored.end();) {
            if (pred(make_pair(*u, cci_value(0)))) {
                m_help_helper->clear_unused(*u);
                u = m_unignored.erase(u);
            } else {
                ++u;
            }
        }
    }

    cci_preset_value_range get_unconsumed_preset_values(const cci_preset_value_predicate& pred) const override
    {
        std::vector<cci_name_value_pair> values;
        for (const auto& name : m_unignored) {
            auto value_it = m_unignored_values.find(name);
            cci_name_value_pair value(
                name, value_it != m_unignored_values.end() ? value_it->second : get_preset_cci_value(name));
            values.emplace_back(std::move(value));
        }
        if (has_parent) {
            for (const auto& value : m_parent.get_unconsumed_preset_values()) {
                values.emplace_back(value);
            }
        }
        return cci_preset_value_range(pred, values);
    }

    // Functions below here require an orriginator to be passed to the local
    // method variant.

    void set_preset_cci_value(const std::string& parname, const cci_value& cci_value,
                              const cci_originator& originator) override
    {
        m_help_helper->set_unused(parname, originator);
        if (sendToParent(parname)) {
            return m_parent.set_preset_cci_value(parname, cci_value, originator);
        } else {
            // consuming_broker rejects writes to locked preset values. Keep the
            // enumeration cache consistent with the value it retains.
            if (locked.find(parname) == locked.end()) {
                m_unignored.insert(parname);
                m_unignored_values[parname] = cci_value;
            }
            return consuming_broker::set_preset_cci_value(parname, cci_value, originator);
        }
    }

    cci_param_untyped_handle get_param_handle(const std::string& parname,
                                              const cci_originator& originator) const override
    {
        if (sendToParent(parname)) {
            return m_parent.get_param_handle(parname, originator);
        }
        cci_param_if* orig_param = get_orig_param(parname);
        if (orig_param) {
            return cci_param_untyped_handle(*orig_param, originator);
        }
        if (has_parent) {
            return m_parent.get_param_handle(parname, originator);
        }
        return cci_param_untyped_handle(originator);
    }

    std::vector<cci_param_untyped_handle> get_param_handles(const cci_originator& originator) const override
    {
        if (has_parent) {
            std::vector<cci_param_untyped_handle> p_param_handles = m_parent.get_param_handles();
            std::vector<cci_param_untyped_handle> param_handles = consuming_broker::get_param_handles(originator);
            // this is likely to be more efficient the other way round, but it keeps
            // things consistent and means the local (more useful) params will be at
            // the head of the list.
            param_handles.insert(param_handles.end(), p_param_handles.begin(), p_param_handles.end());
            return param_handles;
        } else {
            return consuming_broker::get_param_handles(originator);
        }
    }

    bool is_global_broker() const override { return (!has_parent); }
}; // namespace gs

class PrivateConfigurableBroker : public gs::ConfigurableBroker
{
    std::string m_name;
    unsigned m_name_length;
    std::set<std::string> parent;
    bool sendToParent(const std::string& parname) const override
    {
        if (m_uninitialized_cb) {
            auto pos = parname.find_last_of('.');
            if (pos != std::string::npos) {
                auto p = parname.substr(0, pos);
                if (m_uninitialized_cb && m_initialized.count(p) == 0) {
                    const_cast<PrivateConfigurableBroker*>(this)->m_initialized.insert(p);
                    m_uninitialized_cb(p);
                }
            }
        }

        if (parent.count(parname) > 0) {
            return true;
        }
        if (parname.substr(0, m_name_length) == m_name) {
            return false;
        } else {
            return true;
        }
    }

public:
    PrivateConfigurableBroker(std::string name): gs::ConfigurableBroker(name)
    {
        m_name = hierarchical_name();
        m_name_length = m_name.length();

        auto uncon = m_parent.get_unconsumed_preset_values(
            [&](const std::pair<std::string, cci_value>& iv) { return iv.first.find(m_name + ".") == 0; });
        for (auto p : uncon) {
            parent.insert(p.first);
        }
    }

    virtual std::vector<cci_name_value_pair> get_unconsumed_preset_values() const override
    {
        std::vector<cci_name_value_pair> r;
        for (const auto& u : m_unignored) {
            auto value_it = m_unignored_values.find(u);
            r.emplace_back(u, value_it != m_unignored_values.end() ? value_it->second : get_preset_cci_value(u));
        }
        return r;
    }
    cci_preset_value_range get_unconsumed_preset_values(const cci_preset_value_predicate& pred) const override
    {
        std::vector<cci_name_value_pair> values;
        for (const auto& name : m_unignored) {
            auto value_it = m_unignored_values.find(name);
            cci_name_value_pair value(
                name, value_it != m_unignored_values.end() ? value_it->second : get_preset_cci_value(name));
            values.emplace_back(std::move(value));
        }
        return cci_preset_value_range(pred, values);
    }
};

void cci_clear_unused(cci::cci_broker_handle broker, std::string name);

template <typename T>
std::vector<T> cci_get_vector(cci::cci_broker_handle broker, const std::string base)
{
    std::vector<T> ret;
    for (std::string s : sc_cci_children(base.c_str())) {
        if (std::count_if(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); })) {
            ret.push_back(cci_get<T>(broker, base + "." + s));
        }
    }
    return ret;
}

} // namespace gs

/**
 * @brief Provide support for sc_core::sc_object as a CCI param, based on their fully qualified name
 *
 */
template <>
struct cci::cci_value_converter<sc_core::sc_object*> {
    typedef sc_core::sc_object* type;
    static bool pack(cci::cci_value::reference dst, type const& src)
    {
        dst.set_string(src->name());
        return true;
    }
    static bool unpack(type& dst, cci::cci_value::const_reference src)
    {
        if (!src.is_string()) {
            return false;
        }
        std::string name;
        if (!src.try_get(name)) {
            return false;
        }
        sc_assert(name[0] == '&');
        dst = gs::find_sc_obj(nullptr, name.substr(1));
        return true;
    }
};

/**
 * @brief CCI value converted
 * notice that NO conversion is provided.
 *
 */
template <typename T, typename... U>
struct cci::cci_value_converter<gs::cci_function<T(U...)>> {
    typedef gs::cci_function<T(U...)> type;
    static bool pack(cci::cci_value::reference dst, type const& src)
    {
        // unimplemented
        return false;
    }
    static bool unpack(type& dst, cci::cci_value::const_reference src)
    {
        // unimplemented
        return false;
    }
};

template <>
struct cci::cci_value_converter<gs::ConfigurableBroker*> {
    typedef gs::ConfigurableBroker* type;
    static bool pack(cci_value::reference dst, type const& src)
    {
        dst.set_string(src->name());
        return true;
    }
    static bool unpack(type& dst, cci_value::const_reference src) { return false; }
};

#endif // CCIUTILS_H
