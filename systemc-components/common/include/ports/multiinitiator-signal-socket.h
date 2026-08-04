/*
 * This file is part of libgsutils
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBGSUTILS_PORTS_MULTIINITIATOR_SIGNAL_SOCKET_H
#define _LIBGSUTILS_PORTS_MULTIINITIATOR_SIGNAL_SOCKET_H

#include <systemc>
#include <scp/report.h>
#include "sysc/kernel/sc_time.h"
#include "target-signal-socket.h"
template <typename T = bool>
class MultiInitiatorSignalSocket
    : public sc_core::sc_port<sc_core::sc_signal_inout_if<T>, 0, sc_core::SC_ZERO_OR_MORE_BOUND>,
      public sc_core::sc_module
{
    SCP_LOGGER();
    std::vector<T> vals;
    bool vals_valid = false;
    sc_core::sc_event ev;

public:
    MultiInitiatorSignalSocket(const char* name = "MultiInitiatorSignalSocket")
        : sc_core::sc_port<sc_core::sc_signal_inout_if<T>, 0, sc_core::SC_ZERO_OR_MORE_BOUND>(name)
        , sc_core::sc_module(sc_core::sc_module_name((std::string(name) + "_thread").c_str()))
    {
        SCP_DEBUG(())("Constructor");
        SC_THREAD(writer);
    }
    void writer()
    {
        while (1) {
            while (vals_valid == false) {
                wait(ev);
            }
            std::vector<T> vs;
            vs.swap(vals);
            vals_valid = false;
            for (auto v : vs) {
                for (int i = 0; i < this->size(); i++) {
                    if (auto t = dynamic_cast<TargetSignalSocketProxy<bool>*>(this->operator[](i))) {
                        SCP_DEBUG(())("Writing {} to {}", v, t->get_parent().name());
                    }
                    this->operator[](i)->write(v);
                }
                wait(sc_core::SC_ZERO_TIME); // ensure all immediate events have been processed
            }
        }
    }
    void async_write_vector(const std::vector<T>& vs)
    {
        vals.insert(vals.end(), vs.begin(), vs.end()); // COPY the vector to the end.
        vals_valid = true;
        ev.notify();
    }
};

#endif
