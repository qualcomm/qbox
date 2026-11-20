/*
 * This file is part of libgsutils
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBGSUTILS_PORTS_INITIATOR_SIGNAL_SOCKET_H
#define _LIBGSUTILS_PORTS_INITIATOR_SIGNAL_SOCKET_H

#include <systemc>

template <class T>
using InitiatorSignalSocket = sc_core::sc_port<sc_core::sc_signal_inout_if<T>, 1, sc_core::SC_ZERO_OR_MORE_BOUND>;

template <class T>
using RequiredInitiatorSignalSocket = sc_core::sc_port<sc_core::sc_signal_inout_if<T>, 1,
                                                       sc_core::SC_ONE_OR_MORE_BOUND>;

#endif
