/*
 * Generic bindable objects
 *
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <systemc>
#include "sysc/communication/sc_port.h"

namespace gs {

class bindable_base
{
public:
    virtual ~bindable_base() = default;
    virtual bool try_bind(sc_core::sc_object& obj) = 0;
};

template <class Initiator, typename... Targets>
class bindable : public bindable_base
{
    template <class Target>
    bool try_bind_as(sc_core::sc_object& obj)
    {
        if (auto* compatible = dynamic_cast<Target*>(&obj)) {
            auto* derived = static_cast<Initiator*>(this);

            derived->bind(*compatible);

            return true;
        } else {
            return false;
        }
    }

public:
    bool try_bind(sc_core::sc_object& obj) override
    {
        // try to bind to all targets for the initiator type
        return (try_bind_as<Targets>(obj) || ...);
    }
};

// bindable socket
template <class InitSocket, class... Targets>
class bindable_socket : public InitSocket, public bindable<bindable_socket<InitSocket, Targets...>, Targets...>
{
public:
    using InitSocket::InitSocket;
    using InitSocket::bind;
};

// bindable port
template <class IF, int N = 1, sc_core::sc_port_policy P = sc_core::SC_ZERO_OR_MORE_BOUND>
using bindable_port = bindable_socket<sc_core::sc_port<IF, N, P>, sc_core::sc_export<IF>>;

// bindable export
template <class IF>
class bindable_export : public sc_core::sc_export<IF>, public bindable<bindable_export<IF>, sc_core::sc_port_b<IF>>
{
    using Base = sc_core::sc_export<IF>;

public:
    using Base::Base;
    using Base::bind;

    void bind(sc_core::sc_port_b<IF>& port) { port.bind(*this); }
};

inline bool try_bind(sc_core::sc_object& i_obj, sc_core::sc_object& t_obj)
{
    if (auto* bindable = dynamic_cast<bindable_base*>(&i_obj)) {
        if (bindable->try_bind(t_obj)) {
            return true;
        }
    }

    if (auto* bindable = dynamic_cast<bindable_base*>(&t_obj)) {
        if (bindable->try_bind(i_obj)) {
            return true;
        }
    }

    return false;
}

} // namespace gs