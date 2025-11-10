/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _GREENSOCS_MONITOR_H
#define _GREENSOCS_MONITOR_H

#include <cci_configuration>
#include <systemc>
#include <tlm>
#include <scp/report.h>
#include <scp/helpers.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <tlm_sockets_buswidth.h>
#include <libgssync.h>
#include "crow.h"
#include <thread>
#include <future>
#include <qkmultithread.h>
#include <mcips-plugin.h>
#include <chrono>
#include <atomic>
#include <ports/biflow-socket.h>

#define MAX_ASIO_BUF_LEN (1024ULL * 16ULL)
#define MAX_BUFFER       1000

namespace gs {

template <unsigned int BUSWIDTH = DEFAULT_TLM_BUSWIDTH>
class monitor : public sc_core::sc_module
{
    SCP_LOGGER();

    class biflow_ws
    {
    public:
        std::vector<crow::websocket::connection*> m_conns;
        biflow_socket<biflow_ws> socket;
        std::string buffer;
        const char* name;

    public:
        void bind(biflow_bindable& other)
        {
            socket.bind(other);
            socket.can_receive_any();
        }
        void b_transport(tlm::tlm_generic_payload& txn, sc_core::sc_time& t)
        {
            /* NB we are ignoring the templated type here, as we're simply sending strings, if the type is not a
             * character, this will produce some sort of rubbish! */
            char* ptr = (char*)txn.get_data_ptr();
            std::string data(ptr, txn.get_data_length());
            buffer = (buffer + data);
            if (buffer.size() > MAX_BUFFER) buffer = buffer.substr(buffer.size() - MAX_BUFFER);
            for (auto conn : m_conns) {
                conn->send_text(data);
            }
        }
        void enqueue(std::string data)
        {
            for (auto c : data) {
                socket.enqueue(c);
            }
        }
        biflow_ws(gs::biflow_multibindable& o, const char* n)
            : socket(sc_core::sc_gen_unique_name("monitor_biflow_backend")), name(n)
        {
            socket.register_b_transport(this, &biflow_ws::b_transport);
            bind(o);
        }
        void set_conn(crow::websocket::connection* c)
        {
            m_conns.push_back(c);
            c->send_text(buffer);
        }
        void clear_conn(crow::websocket::connection* c)
        {
            auto i = std::find(m_conns.begin(), m_conns.end(), c);
            if (i != m_conns.end()) {
                m_conns.erase(i);
            }
        }
    };
    std::map<std::string, std::unique_ptr<biflow_ws>> biflows;

public:
    monitor(const sc_core::sc_module_name& nm);

    ~monitor();

private:
    void init_monitor();

    void before_end_of_elaboration() override;

    void end_of_elaboration() override;

    void end_of_simulation() override;

public:
    cci::cci_param<uint32_t> p_server_port;
    cci::cci_param<std::string> p_html_doc_template_dir_path;
    cci::cci_param<std::string> p_html_doc_name;
    cci::cci_param<bool> p_use_html_presentation;
    cci::cci_param<uint32_t> p_refresh_interval_ms;

private:
    crow::SimpleApp m_app;
    std::future<void> m_app_future;
    gs::runonsysc m_sc;
    std::vector<tlm_quantumkeeper_multithread*> m_qks;
    std::vector<McipsPlugin*> m_mcips_plugins;
};
} // namespace gs

extern "C" void module_register();

#endif
