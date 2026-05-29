/*
 * Copyright (c) 2022-2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef RUNONSYSTEMC_H
#define RUNONSYSTEMC_H

#include <systemc>
#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace gs {

class runonsysc : public sc_core::sc_module
{
private:
    // ============================================================
    // Core: shared lifetime state
    // ============================================================
    struct Core {
        // --------------------------------------------------------
        // AsyncJob (private implementation detail)
        // --------------------------------------------------------
        struct AsyncJob {
            using Ptr = std::shared_ptr<AsyncJob>;

            std::function<void()> job;

            std::promise<void> done_promise;
            std::shared_future<void> done_future;

            std::atomic<bool> cancelled{ false };

            explicit AsyncJob(std::function<void()> j)
                : job(std::move(j)), done_future(done_promise.get_future().share())
            {
            }

            void operator()()
            {
                try {
                    if (!cancelled.load(std::memory_order_relaxed)) {
                        job();
                    }
                    done_promise.set_value();
                } catch (...) {
                    try {
                        done_promise.set_exception(std::current_exception());
                    } catch (...) {
                        // Likely that this is due to jobs being canceled (at cleanup)
                    }
                }
            }

            void cancel()
            {
                cancelled.store(true, std::memory_order_relaxed);
                try {
                    done_promise.set_value(); // unblock waiters immediately
                } catch (...) {
                    // Too late to cancel the job, it already threw itself!
                }
            }

            void wait()
            {
                done_future.wait();
                done_future.get(); // rethrows if job threw
            }

            bool is_cancelled() const { return cancelled.load(std::memory_order_relaxed); }
        };

        // --------------------------------------------------------
        // Core state
        // --------------------------------------------------------
        std::thread::id thread_id;

        std::queue<typename AsyncJob::Ptr> async_jobs;
        typename AsyncJob::Ptr running_job;
        std::mutex async_jobs_mutex;

        async_event jobs_handler_event;
        std::atomic<bool> running{ true };

        explicit Core(std::thread::id id): thread_id(id), jobs_handler_event(false) {}
    };

    std::shared_ptr<Core> m_core;

    // ============================================================
    // SystemC job handler thread
    // ============================================================
    void jobs_handler()
    {
        auto core = m_core; // hold shared ownership
        if (!core) return;

        std::unique_lock<std::mutex> lock(core->async_jobs_mutex);

        while (core->running.load(std::memory_order_relaxed)) {
            while (core->running.load(std::memory_order_relaxed) && !core->async_jobs.empty()) {
                core->running_job = core->async_jobs.front();
                core->async_jobs.pop();

                lock.unlock();

                sc_core::sc_unsuspendable();
                (*core->running_job)();
                /* Drain any notify(SC_ZERO_TIME) events the job queued
                 * (e.g. deferred IRQ SC_METHODs) before allowing suspend. */
                wait(sc_core::SC_ZERO_TIME);
                sc_core::sc_suspendable();

                lock.lock();
                core->running_job.reset();
            }

            lock.unlock();
            wait(core->jobs_handler_event);
            lock.lock();
        }

        SC_REPORT_WARNING("RunOnSysc", "Stopped");
    }

    void stop()
    {
        auto core = m_core;
        if (!core) return;

        core->running.store(false, std::memory_order_relaxed);
    }

    void cancel_all()
    {
        auto core = m_core;
        if (!core) return;

        std::lock_guard<std::mutex> lock(core->async_jobs_mutex);

        while (!core->async_jobs.empty()) {
            core->async_jobs.front()->cancel();
            core->async_jobs.pop();
        }

        if (core->running_job) {
            core->running_job->cancel();
            core->running_job.reset();
        }
    }

public:
    // ============================================================
    // Constructor
    // ============================================================
    explicit runonsysc(const sc_core::sc_module_name& n = "run-on-sysc"): sc_module(n)
    {
        m_core = std::make_shared<Core>(std::this_thread::get_id());
        SC_THREAD(jobs_handler);
    }

    // ============================================================
    // Destructor
    // ============================================================
    ~runonsysc() override
    {
        auto core = m_core;
        if (!core) return;

        stop();

        m_core.reset(); // safe: state lives until last user exits
    }

    // ============================================================
    // Public API
    // ============================================================

    bool is_on_sysc() const
    {
        auto core = m_core;
        return core && (std::this_thread::get_id() == core->thread_id);
    }

    void end_of_simulation() override
    {
        auto core = m_core;
        if (!core) return;
        stop();
        cancel_all();
    }

    void fork_on_systemc(std::function<void()> job_entry) { run_on_sysc(job_entry, false); }

    /**
     * @brief Run a job on the SystemC kernel thread
     *
     * @param[in] job_entry The job to run
     * @param[in] wait If true, wait for job completion
     *
     * @return true if the job has been succesfully executed or if `wait`
     *         was false, false if it has been cancelled (see
     *         `RunOnSysC::cancel_all`).
     */
    bool run_on_sysc(std::function<void()> job_entry, bool wait = true)
    {
        auto core = m_core; // snapshot lifetime
        if (!core) return false;

        if (!core->running.load(std::memory_order_relaxed)) return false;

        if (is_on_sysc()) {
            job_entry();
            return true;
        }

        auto job = std::make_shared<typename Core::AsyncJob>(std::move(job_entry));

        {
            std::lock_guard<std::mutex> lock(core->async_jobs_mutex);

            if (!core->running.load(std::memory_order_relaxed)) return false;

            core->async_jobs.push(job);
        }

        core->jobs_handler_event.async_notify();

        if (wait) {
            try {
                job->wait();
            } catch (...) {
                auto old = sc_core::sc_report_handler::set_actions(sc_core::SC_ERROR,
                                                                   sc_core::SC_LOG | sc_core::SC_DISPLAY);
                SC_REPORT_ERROR("RunOnSysc", "Run on systemc received an unknown exception from job");
                sc_core::sc_report_handler::set_actions(sc_core::SC_ERROR, old);
                stop();
                // Notify the job handler so that it can jump out of it's loop.
                core->jobs_handler_event.async_notify();
                return false;
            }

            return !job->is_cancelled();
        }

        return true;
    }
};

} // namespace gs

#endif // RUNONSYSTEMC_H
