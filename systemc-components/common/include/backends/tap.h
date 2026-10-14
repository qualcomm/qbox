/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <mutex>
#include <queue>

#include <backends/net-backend.h>

#include <async_event.h>

class NetworkBackendTap : public NetworkBackend, public sc_core::sc_module
{
private:
    gs::async_event m_event;
    std::queue<Payload*> m_queue;
    std::mutex m_mutex;
    int m_fd;

    void open(std::string& tun);
    void rcv_thread();
    void rcv();
    void close();

public:
    NetworkBackendTap(sc_core::sc_module_name name, std::string tun);

    virtual ~NetworkBackendTap();

    void send(Payload& frame);
};
