/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QKMULTI_MULTI_QUANTUM_H
#define QKMULTI_MULTI_QUANTUM_H

#include <qkmultithread.h>

namespace gs {
class tlm_quantumkeeper_multi_quantum : public tlm_quantumkeeper_multithread
{
    // Only allow up to one quantum from the current sc_time
    // In accordance with TLM-2
    virtual sc_core::sc_time time_to_sync() override
    {
        sc_core::sc_time quantum = tlm_utils::tlm_quantumkeeper::get_global_quantum();
        sc_core::sc_time next_quantum_boundary = sc_core::sc_time_stamp() + quantum;
        sc_core::sc_time now = get_current_time();
        if (now < next_quantum_boundary) {
            return next_quantum_boundary - now;
        } else if (now == next_quantum_boundary) {
            // At the exact boundary: the CPU has used its quantum but hasn't
            // exceeded it. Return a minimal non-zero value so sync() doesn't
            // block. The timehandler will advance the kernel on the next delta
            // cycle; blocking here risks a deadlock when sc_suspend_all()
            // prevents next_time() from seeing the scheduled tick event.
            return sc_core::sc_time(1, sc_core::SC_PS);
        } else {
            return sc_core::SC_ZERO_TIME;
        }
    }

    // Don't sync until we are on the quantum boundry (as expected by TLM-2)
    virtual bool need_sync() override { return time_to_sync() == sc_core::SC_ZERO_TIME; }
};
} // namespace gs
#endif