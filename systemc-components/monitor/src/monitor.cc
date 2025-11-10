/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

/**
 * Example configuration:
 *

platform["monitor_0"] = {
    moduletype = "monitor";
    server_port = 18080;
    use_html_presentation = true;
    html_doc_template_dir_path = "/path/to/html/templates";
    html_doc_name = "monitor.html";
    refresh_interval_ms = 100;
};

 */

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#endif
#include "monitor.h"
#include <module_factory_registery.h>
#include <router_if.h>
#include <vector>
#include <functional>
#include <exception>
#include <cciutils.h>
#include <algorithm>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <filesystem>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

std::string get_html_base_dir()
{
    char result[1024] = { 0 };
#ifdef __APPLE__
    uint32_t size = sizeof(result);
    if (_NSGetExecutablePath(result, &size) != 0) {
        std::cerr << "error executing _NSGetExecutablePath() in monitor.cc" << std::endl;
        exit(EXIT_FAILURE);
    }
#elif __linux__
    ssize_t count = readlink("/proc/self/exe", result, 1024);
    if (count < 0) {
        std::cerr << "error executing readlink(\"/proc/self/exe\") in monitor.cc: " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }
#elif defined(_WIN32)
    DWORD count = GetModuleFileNameA(NULL, result, sizeof(result));
    if (count == 0 || count == sizeof(result)) {
        std::cerr << "error executing GetModuleFileNameA() in monitor.cc" << std::endl;
        exit(EXIT_FAILURE);
    }
#else
#error monitor supports only Mac OS, Linux, and Windows!
#endif
    const std::filesystem::path base_exec_path = std::filesystem::path(result).parent_path();
    const std::string static_html_base_dir = "static";
    const std::filesystem::path html_base_path = base_exec_path / static_html_base_dir;
    return html_base_path.string();
}

namespace gs {

template <unsigned int BUSWIDTH>
monitor<BUSWIDTH>::monitor(const sc_core::sc_module_name& nm)
    : sc_core::sc_module(nm)
    , p_server_port("server_port", 18080, "monitor server port number")
    , p_html_doc_template_dir_path("html_doc_template_dir_path", get_html_base_dir(),
                                   "path to a template directory where HTML document to call the REST API exist")
    , p_html_doc_name("html_doc_name", "monitor.html", "name of a HTML document to call the REST API")
    , p_use_html_presentation("use_html_presentation", true, "use HTML document to present the REST API")
    , p_refresh_interval_ms("refresh_interval_ms", 100, "refresh interval in milliseconds for the web interface")
{
    SCP_DEBUG(()) << "monitor constructor";
    m_app.signal_clear();
    init_monitor();
}

template <unsigned int BUSWIDTH>
monitor<BUSWIDTH>::~monitor()
{
    m_app.stop();
}

std::vector<crow::json::wvalue> json_cci_params(sc_core::sc_object* obj)
{
    std::string name = obj->name();
    cci_broker_handle m_broker = (sc_core::sc_get_current_object())
                                     ? cci_get_broker()
                                     : cci_get_global_broker(cci_originator("gs__sc_cci_children"));
    std::set<std::string> cldr;
    for (sc_core::sc_object* child : obj->get_child_objects()) {
        if (child->kind() == std::string("sc_module")) {
            cldr.insert(child->name());
        }
    }

    int l = name.length() + 1;
    auto uncon = m_broker.get_unconsumed_preset_values([&](const std::pair<std::string, cci_value>& iv) {
        if (iv.first.find(name + ".") == 0) {
            std::string namep = iv.first.substr(0, iv.first.find(".", l));
            return (cldr.find(namep) == cldr.end());
        }
        return false;
    });
    cci_param_predicate pred([&](const cci_param_untyped_handle& p) {
        std::string p_name = p.name();
        if (p_name.find(name + ".") == 0) {
            std::string namep = p_name.substr(0, p_name.find(".", l));
            return (cldr.find(namep) == cldr.end());
        }
        return false;
    });
    auto cons = m_broker.get_param_handles(pred);

    std::vector<crow::json::wvalue> children;
    for (auto p : uncon) {
        children.push_back({ { "name", p.first.substr(l) },
                             { "value", crow::json::load(p.second.to_json()) },
                             { "basename", p.first } });
    }
    for (auto p : cons) {
        children.push_back({ { "basename", std::string(p.name()).substr(l) },
                             { "value", crow::json::load(p.get_cci_value().to_json()) },
                             { "name", p.name() },
                             { "description", p.get_description() } });
    }

    return children;
}

crow::json::wvalue object_to_json(sc_core::sc_object* obj)
{
    return { { "basename", obj->basename() }, { "name", obj->name() }, { "kind", obj->kind() } };
}

crow::json::wvalue json_object(sc_core::sc_object* obj)
{
    crow::json::wvalue r = object_to_json(obj);
    if (obj->kind() == std::string("tlm_target_socket")) {
        if (dynamic_cast<tlm::tlm_base_target_socket_b<>*>(obj)) {
            // * this object is explorable - treat as memory.

            uint8_t data[8];
            tlm::tlm_generic_payload txn;
            txn.set_command(tlm::TLM_READ_COMMAND);
            txn.set_address(0);
            txn.set_data_ptr(reinterpret_cast<unsigned char*>(&data));
            txn.set_data_length(8);
            txn.set_streaming_width(8);
            txn.set_byte_enable_length(0);
            txn.set_dmi_allowed(false);
            txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

            int size = dynamic_cast<tlm::tlm_base_target_socket_b<>*>(obj)->get_base_export()->transport_dbg(txn);

            if (size) {
                r["dbg_port"] = true;
            }
        }
    }

    std::vector<crow::json::wvalue> params = json_cci_params(obj);
    if (params.size()) {
        r["cci_params"] = crow::json::wvalue::list(params);
    }

    std::vector<crow::json::wvalue> co;
    std::vector<crow::json::wvalue> cm;
    std::vector<crow::json::wvalue> cs;

    for (sc_core::sc_object* child : obj->get_child_objects()) {
        co.push_back(object_to_json(child));
        if (child->kind() == std::string("sc_module")) {
            cm.push_back(object_to_json(child));
        }

        if (child->kind() == std::string("tlm_target_socket")) {
            if (dynamic_cast<tlm::tlm_base_target_socket_b<>*>(child)) {
                // * this object is explorable - treat as memory.

                uint8_t data[8];
                tlm::tlm_generic_payload txn;
                txn.set_command(tlm::TLM_READ_COMMAND);
                txn.set_address(0);
                txn.set_data_ptr(reinterpret_cast<unsigned char*>(&data));
                txn.set_data_length(8);
                txn.set_streaming_width(8);
                txn.set_byte_enable_length(0);
                txn.set_dmi_allowed(false);
                txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

                int size = dynamic_cast<tlm::tlm_base_target_socket_b<>*>(child)->get_base_export()->transport_dbg(txn);

                if (size) {
                    cs.push_back(object_to_json(child));
                }
            }
        }
    }
    if (co.size()) {
        r["child_objects"] = crow::json::wvalue::list(co);
    }
    if (cm.size()) {
        r["child_modules"] = crow::json::wvalue::list(cm);
    }
    if (cs.size()) {
        r["child_dbg_ports"] = crow::json::wvalue::list(cs);
    }
    if (dynamic_cast<router_if<>*>(obj)) {
        auto router = dynamic_cast<router_if<>*>(obj);
        std::vector<crow::json::wvalue> pm;
        for (auto t : router->get_bound_targets()) {
            crow::json::wvalue i = { { "name", gs::get_parent_name(t->name.c_str()) },
                                     { "address", static_cast<uint64_t>(t->address) },
                                     { "size", static_cast<uint64_t>(t->size) },
                                     { "priority", t->priority } };
            pm.push_back(i);
        }
        r["port_map"] = crow::json::wvalue::list(pm);
    }

    return r;
}

template <unsigned int BUSWIDTH>
void monitor<BUSWIDTH>::init_monitor()
{
    CROW_ROUTE(m_app, "/")
    ([&]() {
        if (!p_html_doc_template_dir_path.get_value().empty() && !p_html_doc_name.get_value().empty() &&
            p_use_html_presentation.get_value()) {
            crow::mustache::set_global_base(p_html_doc_template_dir_path.get_value());
            auto page = crow::mustache::load(p_html_doc_name.get_value());
            return page.render_string();
        } else {
            std::string ret =
                "API:\n/sc_time\n/pause\n/continue\n/reset\n/object/\n//object/<str>\n/mcips_plugin_status\n/"
                "qk_status\n/sc_suspended\n/refresh_interval\n/"
                "transport_dbg/<int>/<str>";
            return ret;
        }
    });
    CROW_ROUTE(m_app, "/res/<string>")
    ([&](const std::string& file) {
        std::string relative_file_path = "res/" + file;

        if (!p_html_doc_template_dir_path.get_value().empty() &&
            std::filesystem::exists(p_html_doc_template_dir_path.get_value() + "/" + relative_file_path)) {
            crow::mustache::set_global_base(p_html_doc_template_dir_path.get_value());
            auto page = crow::mustache::load(relative_file_path);
            return page.render_string();
        } else {
            return "Invalid file: " + relative_file_path;
        }
    });
    CROW_ROUTE(m_app, "/sc_time")
    ([&]() {
        crow::json::wvalue r;
        m_sc.run_on_sysc([&] { r["sc_time_stamp"] = sc_core::sc_time_stamp().to_seconds(); });
        return r;
    });
    CROW_ROUTE(m_app, "/pause")
    ([&]() {
        crow::json::wvalue ret;
        m_sc.run_on_sysc([&] {
            sc_core::sc_suspend_all();
            ret["sc_time_stamp"] = sc_core::sc_time_stamp().to_seconds();
        });
        return ret;
    });
    CROW_ROUTE(m_app, "/continue")
    ([&]() {
        crow::json::wvalue ret;
        m_sc.run_on_sysc([&] {
            sc_core::sc_unsuspend_all();
            ret["sc_time_stamp"] = sc_core::sc_time_stamp().to_seconds();
        });
        return ret;
    });
    CROW_ROUTE(m_app, "/object/")
    ([&]() {
        std::vector<crow::json::wvalue> cr;
        m_sc.run_on_sysc([&] {
            for (sc_core::sc_object* child : sc_core::sc_get_top_level_objects()) {
                if (child->kind() == std::string("sc_module")) {
                    cr.push_back(json_object(child));
                }
            }
        });
        crow::json::wvalue r = cr;
        return r;
    });
    CROW_ROUTE(m_app, "/object/<str>")
    ([&](std::string name) {
        crow::json::wvalue r;
        m_sc.run_on_sysc([&] {
            auto o = find_sc_obj(nullptr, name, true);
            if (o != nullptr) {
                r = json_object(o);
            } else {
                r["error"] = "Object not found";
            }
        });
        return r;
    });
    CROW_ROUTE(m_app, "/qk_status")
    ([&]() {
        std::vector<crow::json::wvalue> cr;
        for (auto q : m_qks) {
            cr.push_back(crow::json::load(q->get_status_json()));
        }
        crow::json::wvalue r = crow::json::wvalue::list(cr);
        return r;
    });
    CROW_ROUTE(m_app, "/mcips_plugin_status")
    ([&]() {
        std::ostringstream os;
        os << "[";
        bool firstPlugin = true;
        for (auto* p : m_mcips_plugins) {
            if (!firstPlugin) os << ",";
            firstPlugin = false;
            os << p->get_mcips_status_json(); // returns a JSON string per plugin
        }
        os << "]";
        crow::response res(os.str());
        res.add_header("Content-Type", "application/json");
        return res;
    });
    CROW_ROUTE(m_app, "/sc_suspended")
    ([&]() {
        crow::json::wvalue r;
        r["sc_suspended"] = (sc_core::sc_get_status() == sc_core::SC_SUSPENDED);
        return r;
    });
    CROW_ROUTE(m_app, "/refresh_interval")
    ([&]() {
        crow::json::wvalue r;
        r["refresh_interval_ms"] = p_refresh_interval_ms.get_value();
        return r;
    });
    CROW_ROUTE(m_app, "/transport_dbg/<int>/<str>")
    ([&](uint64 addr, std::string name) {
        crow::json::wvalue r;

        auto sc_obj = gs::find_sc_obj(nullptr, name, true);
        auto exp = dynamic_cast<tlm::tlm_base_target_socket_b<>*>(sc_obj);
        if (!exp) {
            r["error"] = "Object not a tlm base target socket";
            return r;
        }
        uint32_t data;
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_address(0);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&data));
        txn.set_data_length(4);
        txn.set_streaming_width(4);
        txn.set_byte_enable_length(0);
        txn.set_dmi_allowed(false);
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

        int size = exp->get_base_export()->transport_dbg(txn);
        if (size != 4) {
            r["error"] = "Unable to get data";
        } else {
            r["value"] = data;

            r["size"] = size;
        }
        return r;
    });
    CROW_ROUTE(m_app, "/biflows")
    ([&]() {
        std::vector<std::string> biflownames;
        for (auto& b : biflows) {
            biflownames.push_back(b.first);
        }
        crow::json::wvalue r;
        r["biflows"] = biflownames;
        return r;
    });
    CROW_ROUTE(m_app, "/biflow/<str>")
        .websocket(&m_app)
        .onaccept([&](const crow::request& req, void** userdata) {
            std::string name = req.url.substr(req.url.find_last_of('/') + 1);
            if (biflows.count(name)) {
                *userdata = &(biflows[name]);
                return true;
            } else {
                SCP_WARN(())("Biflow socket {} not found", name);
                return false;
            }
        })
        .onopen([&](crow::websocket::connection& conn) {
            auto b = (static_cast<std::unique_ptr<biflow_ws>*>(conn.userdata()));
            (*b)->set_conn(&conn);
        })
        .onmessage([&](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
            auto b = (static_cast<std::unique_ptr<biflow_ws>*>(conn.userdata()));
            m_sc.run_on_sysc([&] { (*b)->enqueue(data); });
        })
        .onclose([&](crow::websocket::connection& conn, const std::string& reason, uint16_t) {
            auto b = (static_cast<std::unique_ptr<biflow_ws>*>(conn.userdata()));
            (*b)->clear_conn(&conn);
        })
        .onerror([&](crow::websocket::connection& conn, const std::string& reason) {
            auto b = (static_cast<std::unique_ptr<biflow_ws>*>(conn.userdata()));
            (*b)->clear_conn(&conn);
        });
    m_app_future = m_app.loglevel(crow::LogLevel::Error).port(p_server_port.get_value()).concurrency(1).run_async();
}

template <unsigned int BUSWIDTH>
void monitor<BUSWIDTH>::before_end_of_elaboration()
{
    for (auto b : find_sc_objects<gs::biflow_multibindable>()) {
        biflows[b->name()] = std::make_unique<biflow_ws>(*b, b->name());
        SCP_INFO(())("Found a biflow {}", b->name());
    }
}

template <unsigned int BUSWIDTH>
void monitor<BUSWIDTH>::end_of_elaboration()
{
    m_qks = find_sc_objects<gs::tlm_quantumkeeper_multithread>();
    m_mcips_plugins = find_sc_objects<McipsPlugin>();
}

template <unsigned int BUSWIDTH>
void monitor<BUSWIDTH>::end_of_simulation()
{
    m_app.stop();
}

template class monitor<32>;
template class monitor<64>;
} // namespace gs
typedef gs::monitor<32> monitor;
void module_register() { GSC_MODULE_REGISTER_C(monitor); }
