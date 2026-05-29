/*
 * This file is part of libqbox
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Bridges QEMU's C plugin callbacks into the owning C++ plugin instance.
 *
 * Every QEMU plugin callback carries a `void* userdata`, which we set to the
 * plugin's `PluginHandle*`. A subclass registers a capture-less lambda directly
 * with the plugin API (passing `handle_as_userdata()`) and, inside the lambda,
 * calls `dispatch_userdata()` to recover the `LibQemuPlugin*` and run its
 * logic. Dispatch is a single path with zero lookup.
 *
 * The PluginHandle is heap-allocated and intentionally leaked at plugin
 * destruction so that any callback QEMU still has registered finds
 * `alive == false` and returns early instead of dereferencing a freed
 * plugin object. Every dispatch bumps the same `m_handle->refcount`, which
 * `shutdown_bridge` drains to serialize destruction with in-flight
 * callbacks.
 */

#ifndef _LIBQBOX_COMPONENTS_LIBQEMU_PLUGIN_H
#define _LIBQBOX_COMPONENTS_LIBQEMU_PLUGIN_H

#include <systemc>

#include <cciutils.h>
#include <libqemu-cxx/libqemu-cxx.h>

#include <scp/report.h>
#include <cci_configuration>

#include <atomic>
#include <thread>

constexpr double NSEC_IN_ONE_SEC = 1e9;

class LibQemuPlugin;

/**
 * @brief Stable userdata passed to every plugin callback.
 *
 * Heap-allocated when the plugin is constructed and intentionally leaked
 * when it is destroyed. Outliving the plugin lets stale callbacks observe
 * @c alive == false and exit early without dereferencing freed memory.
 */
struct PluginHandle {
    LibQemuPlugin* self{ nullptr };  // valid while @c alive == true
    std::atomic<bool> alive{ true }; // false once shutdown_bridge() returns
    std::atomic<int> refcount{ 0 };  // in-flight bridge callbacks
};

/**
 * @brief Base class for QBox C++ plugins that bridge QEMU plugin callbacks.
 */
class LibQemuPlugin : public sc_core::sc_module
{
    SCP_LOGGER();

protected:
    cci::cci_param<uint64_t> m_id; // unique plugin id
    qemu::LibQemu& m_inst;

    // Leaked at destruction so stale callbacks find @c alive==false.
    PluginHandle* m_handle;

public:
    LibQemuPlugin(const sc_core::sc_module_name& nm, qemu::LibQemu& inst)
        : sc_module(nm), m_inst(inst), m_id("id", 0, "qemu plugin id"), m_handle(new PluginHandle())
    {
        m_handle->self = this;
    }

    virtual ~LibQemuPlugin() { shutdown_bridge(); }

    // Push the `-plugin <path>,key=<id>` argument to libqemu's command line.
    // Called from the subclass after QemuInstance has finished pushing its
    // own libqbox arguments.
    void push_plugin_args(const std::string& plugin_path)
    {
        SCP_DEBUG(())("push_plugin_args, key: {} ", m_id.name());
        SCP_DEBUG(())("push_plugin_args, plugin_path: {} ", plugin_path);
        std::stringstream opts;
        opts << plugin_path;
        opts << ",key=" << m_id.name();
        m_inst.push_qemu_arg("-plugin");
        m_inst.push_qemu_arg(opts.str().c_str());
    }

    /**
     * @brief Stop bridge dispatch and drain in-flight callbacks.
     *
     * `m_handle->alive`'s exchange is the once-guard. Flips the
     * handle dead so new dispatches bail, then waits for the shared refcount
     * to drain. The handle itself is intentionally leaked.
     */
    void shutdown_bridge()
    {
        if (!m_handle->alive.exchange(false, std::memory_order_seq_cst)) {
            return;
        }
        while (m_handle->refcount.load(std::memory_order_acquire) > 0) {
            std::this_thread::yield();
        }
    }

    // Pass this as @c userdata when registering any plugin callback.
    void* handle_as_userdata() const { return m_handle; }

    /**
     * @brief Run @p body on the live plugin instance, holding a refcount for
     *        the duration. No-op when the handle is null or @c alive==false.
     *
     * Every subclass callback funnels through here:
     *
     *     m_inst.plugin_api().qemu_plugin_register_vcpu_init_cb(
     *         m_id,
     *         [](int idx, void* userdata) {
     *             LibQemuPlugin::dispatch_userdata(userdata, [&](LibQemuPlugin* p) {
     *                 static_cast<MyPlugin*>(p)->my_cb(idx);
     *             });
     *         },
     *         handle_as_userdata());
     */
    template <typename Body>
    static void dispatch_userdata(void* userdata, Body body)
    {
        auto* h = static_cast<PluginHandle*>(userdata);
        if (!h || !h->alive.load(std::memory_order_acquire)) return;
        h->refcount.fetch_add(1, std::memory_order_seq_cst);
        if (h->alive.load(std::memory_order_seq_cst)) {
            body(h->self);
        }
        h->refcount.fetch_sub(1, std::memory_order_seq_cst);
    }
};

#endif // _LIBQBOX_COMPONENTS_LIBQEMU_PLUGIN_H
