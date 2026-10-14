/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <thread>
#include <unistd.h>
#ifdef __APPLE__
#include <net/if_utun.h> // UTUN_CONTROL_NAME
#else
#include <linux/if_tun.h>
#endif
#include <errno.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <scp/report.h>

#include "backends/tap.h"

using namespace sc_core;

NetworkBackendTap::NetworkBackendTap(sc_core::sc_module_name name, std::string tun): sc_module(name)
{
    SCP_TRACE(SCMOD) << "Constructor";
    m_fd = -1;
    open(tun);

    SC_METHOD(rcv);
    sensitive << m_event;
    dont_initialize();
}

NetworkBackendTap::~NetworkBackendTap() { close(); }

void NetworkBackendTap::open(std::string& tun)
{
    struct ifreq ifr;

    if ((m_fd = ::open("/dev/net/tun", O_RDWR)) < 0) {
        SCP_ERR(SCMOD) << "Failed to open /dev/net/tun: " << strerror(errno);
        return;
    }
#ifndef __APPLE__
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    strncpy(ifr.ifr_name, tun.c_str(), IFNAMSIZ - 1);
    if (ioctl(m_fd, TUNSETIFF, (void*)&ifr) < 0) {
        SCP_WARN(SCMOD) << ifr.ifr_name << ": tap device setup failed: " << strerror(errno);
        ::close(m_fd);
        m_fd = -1;
        return;
    }
#endif
    SCP_DEBUG(SCMOD) << "TAP opened";

    new std::thread(&NetworkBackendTap::rcv_thread, this);
}

void NetworkBackendTap::close()
{
    if (m_fd < 0) {
        return;
    }
    ::close(m_fd);
    m_fd = -1;
}

void NetworkBackendTap::rcv_thread()
{
    for (;;) {
        Payload* frame = new Payload(9000);

        int r = ::read(m_fd, frame->data(), (int)frame->capacity());
        if (r > 0) {
            if (r < 60) {
                /*
                 * Pad with zeroes as the minimal payload size is 60 bytes
                 * (60 bytes of data + 4 bytes of crc -> 64bytes)
                 */
                std::memset(frame->data() + r, 0, 60 - r);
                r = 60;
            }
            frame->resize(r);
        } else {
            /* error */
            delete frame;
        }

        std::lock_guard<std::mutex> lock(m_mutex);

        if (r > 0) {
            SCP_TRACE(SCMOD) << "frame of size " << r << " EXT -> VP";
            m_queue.push(frame);
        }
        if (!m_queue.empty()) {
            m_event.async_notify();
        }
    }
}

void NetworkBackendTap::rcv()
{
    Payload* frame;

    std::lock_guard<std::mutex> lock(m_mutex);

    while (!m_queue.empty()) {
        if (m_can_receive(m_opaque)) {
            frame = m_queue.front();
            m_queue.pop();
            m_receive(m_opaque, *frame);
            delete frame;
        } else {
            /* notify myself later, hopefully the queue drains */
            m_event.notify(sc_core::sc_time(1, sc_core::SC_MS));
            return;
        }
    }
}

void NetworkBackendTap::send(Payload& frame)
{
    if (m_fd < 0) {
        return;
    }
    SCP_TRACE(SCMOD) << "frame of size " << frame.size() << " VP -> EXT";
    // ::write(m_fd, frame.data(), (int)frame.size());

    if (::write(m_fd, frame.data(), (int)frame.size()) != 1) {
        SCP_WARN(SCMOD) << "Write did not complete: " << strerror(errno);
    }
}
