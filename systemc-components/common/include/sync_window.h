/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>
#include <tlm>
#include <cci_configuration>
#include <tlm_utils/tlm_quantumkeeper.h>
#include <scp/report.h>
#include <mutex>
#include <type_traits>

#ifdef SC_HAS_OB_EVENT
#include <sysc/utils/sc_ob_event.h>
#else
#include "observer_event.h"
typedef gs::observer_event sc_ob_event;
#endif

namespace sc_core {

/**
 * @brief Base class which gives the quantum (dynamically) and specifies if the
 * window should stay open indefinitely or detach when there are no more events.
 * A SC_ZERO_TIME quantum means sync on every time step
 */
struct sc_sync_policy_base {
    virtual sc_core::sc_time quantum() = 0;
    virtual bool keep_alive() = 0;
};
struct sc_sync_policy_tlm_quantum : public sc_sync_policy_base {
    sc_core::sc_time quantum() { return tlm_utils::tlm_quantumkeeper::get_global_quantum(); }
    bool keep_alive() { return true; }
};
struct sc_sync_policy_in_sync : public sc_sync_policy_base {
    sc_core::sc_time quantum()
    {
        return sc_core::sc_pending_activity() ? sc_core::sc_time_to_pending_activity() : sc_core::SC_ZERO_TIME;
    }
    bool keep_alive() { return false; }
};
/**
 * @brief windowed synchronizer, template SYNC_POLICY gives the quantum (dynamically) and specifies if the
 * window should stay open indefinitely or detach when there are no more events.
 *
 * @tparam sc_sync_policy
 */
template <class sc_sync_policy = sc_sync_policy_in_sync>
class sc_sync_window : public sc_core::sc_module, public sc_core::sc_prim_channel
{
    static_assert(std::is_base_of<sc_sync_policy_base, sc_sync_policy>::value,
                  "sc_sync_policy must derive from sc_sync_policy_base");

    sc_core::sc_event m_sweep_ev;
    sc_ob_event m_step_ev;
    sc_event m_update_ev;
    std::mutex m_mutex;
    sc_sync_policy policy;

public:
    struct window {
        sc_core::sc_time from;
        sc_core::sc_time to;
        bool operator==(const window& other) const { return other.to == to && other.from == from; }
    };

    static inline const struct window zero_window = { sc_core::SC_ZERO_TIME, sc_core::SC_ZERO_TIME };
    static inline const struct window open_window = { sc_core::SC_ZERO_TIME, sc_core::sc_max_time() };

private:
    window m_window;
    window m_incomming_window; // used to hold the window values coming in from
                               // the other side.
    bool m_is_attached;        // Track attachment state

    std::function<void(const window&)> m_other_async_set_window_fn;

    void do_other_async_set_window_fn(window w)
    {
        if (m_other_async_set_window_fn) {
            auto now = sc_core::sc_time_stamp();
            m_other_async_set_window_fn(w);
        }
    }

    /* Handle suspending/resuming at the end of the window, also inform other side if we reach end of window */
    void step_helper()
    {
        auto now = sc_core::sc_time_stamp();
        auto to = m_window.to;
        SCP_INFO(()) << "End window check"
                     << " their window " << m_window.from << " " << to;

        if (now >= to) {
            sc_core::sc_unsuspend_all(); // such that pending activity is valid if it's needed below.

            /* We should suspend at this point, and wait for the other side to catch up */
            SCP_TRACE(()) << "Suspend at: " << now << " quantum: " << policy.quantum();
            do_other_async_set_window_fn({ now, now + policy.quantum() });

            if (!policy.keep_alive()) async_attach_suspending();
            sc_core::sc_suspend_all();

        } else {
            sc_core::sc_unsuspend_all();
            if (!policy.keep_alive()) async_detach_suspending();

            /* Re-notify event - maybe presumably moved */
            m_step_ev.notify(to - now);
        }
    }

    /*
     * Handle all sweep requests, once we arrive at a sweep point, tell the other side.
     */
    void sweep_helper()
    {
        auto now = sc_core::sc_time_stamp();

        SCP_INFO(()) << "Done sweep"
                     << " their window " << m_window.from << " - " << m_window.to;

        do_other_async_set_window_fn({ now, now + policy.quantum() });
    }

    /* Manage the Sync aync update  */
    void update()
    {
        std::lock_guard<std::mutex> lg(m_mutex);
        // Now we are on our thread, it's safe to update our window.
        m_window = m_incomming_window;
        auto now = sc_core::sc_time_stamp();
        SCP_INFO(()) << "Update"
                     << " their window " << m_window.from << " - " << m_window.to;

        if (m_window.from > now) {
            m_sweep_ev.notify(m_window.from - now); // Try to move time forward.
        } else {
            m_sweep_ev.cancel(); // no need to fire event.
        }
        /* let stepper handle suspend/resume, must time notify */
        m_update_ev.notify(sc_core::SC_ZERO_TIME);
    }

public:
    SCP_LOGGER();

    /* API call from other pair, presumably in a different thread.
     * The internal window wil be updated atomically.
     *
     * Input: window  - Window to set for sync. Sweep till the 'from' and step to the 'to'.
     */
    void async_set_window(const window& w)
    {
        /* Only accept updated windows so we dont re-send redundant updates */
        std::lock_guard<std::mutex> lg(m_mutex);
        // Check if window is attached before allowing window updates
        if (!m_is_attached) {
            SCP_FATAL(()) << "Cannot set window on detached sync_window. "
                          << "Window must be attached before calling async_set_window(). ";
        }
        m_incomming_window = w;
        if (!(w == m_window)) {
            SCP_INFO(()) << "sync " << w.from << " " << w.to;
            async_request_update();
        }
    }
    /**
     * @brief Check if the sync_window is currently attached.
     * @return true if attached, false if detached
     */
    bool is_attached() const { return m_is_attached; }
    /**
     * @brief Attach to the primitive channel's suspend/resume mechanism for time synchronization.
     *
     * When attached, this sync_window actively participates in SystemC's suspend/resume protocol,
     * allowing it to suspend simulation when synchronization windows are reached.
     */
    void attach()
    {
        async_attach_suspending();
        m_is_attached = true;
    }
    /**
     * @brief Detach from the primitive channel's suspend/resume mechanism and fully open the time window.
     *
     * When detached, this sync_window no longer participates in suspend/resume, meaning it cannot
     * block simulation time advancement. Additionally, the synchronization window is set to
     * [now, sc_max_time()], effectively removing all time restrictions and allowing the simulation
     * to advance freely without waiting for the paired sync_window.
     *
     * @param now Starting time for the unrestricted window (defaults to SC_ZERO_TIME)
     */
    void detach(const sc_core::sc_time now = sc_core::SC_ZERO_TIME)
    {
        async_set_window({ now, sc_core::sc_max_time() }); // setting open window before detaching, since
                                                           // async_set_window required an attached window.
        async_detach_suspending();
        m_is_attached = false;
    }
    void bind(sc_sync_window* other)
    {
        if (m_other_async_set_window_fn) {
            SC_REPORT_WARNING("sc_sync_window",
                              "m_other_async_set_window_fn was already registered or other "
                              "sc_sync_window was already bound!");
        }
        m_other_async_set_window_fn = std::bind(&sc_sync_window::async_set_window, other, std::placeholders::_1);
    }
    void register_sync_cb(std::function<void(const window&)> fn)
    {
        if (m_other_async_set_window_fn) {
            SC_REPORT_WARNING("sc_sync_window",
                              "m_other_async_set_window_fn was already registered or other "
                              "sc_sync_window was already bound!");
        }
        m_other_async_set_window_fn = fn;
    }
    SC_CTOR (sc_sync_window) : m_window({sc_core::SC_ZERO_TIME, policy.quantum()}), m_is_attached(false)
        {
            SCP_TRACE(())("Constructor");
            SC_METHOD(sweep_helper);
            dont_initialize();
            sensitive << m_sweep_ev;

            SC_METHOD(step_helper);
            dont_initialize();
            sensitive << m_step_ev << m_update_ev;

            m_step_ev.notify(sc_core::SC_ZERO_TIME);

            this->attach(); // This will set m_is_attached to true
        }
};

} // namespace sc_core