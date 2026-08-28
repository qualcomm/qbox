/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _LIBGSUTILS_PORTS_TARGET_SIGNAL_SOCKET_H
#define _LIBGSUTILS_PORTS_TARGET_SIGNAL_SOCKET_H

#include <functional>
#include <cassert>
#include <async_event.h>
#include <systemc>

template <class T>
class TargetSignalSocket;

template <class T>
class TargetSignalSocketProxy : public sc_core::sc_signal_inout_if<T>
{
public:
    using Iface = sc_core::sc_signal_inout_if<T>;
    using ValueChangedCallback = std::function<void(const T&)>;

protected:
    TargetSignalSocket<T>& m_parent;

    T m_val;
    ValueChangedCallback m_cb;
    gs::async_event m_ev;

public:
    TargetSignalSocketProxy(TargetSignalSocket<T>& parent): m_parent(parent), m_val{}, m_cb(nullptr), m_ev(false) {}

    void register_value_changed_cb(const ValueChangedCallback& cb) { m_cb = cb; }

    TargetSignalSocket<T>& get_parent() { return m_parent; }

    void notify() { m_ev.notify(); }

    /* SystemC interfaces */

    /* sc_core::sc_signal_in_if<T> */
    virtual const sc_core::sc_event& default_event() const { return m_ev; }

    virtual const sc_core::sc_event& value_changed_event() const { return m_ev; }

    virtual const T& read() const { return m_val; }

    virtual const T& get_data_ref() const { return m_val; }

    virtual bool event() const
    {
        /* Not implemented */
        assert(false);
        return false;
    }

    /* sc_core::sc_signal_write_if<T> */
    virtual void write(const T& val)
    {
        bool changed = (val != m_val);

        m_val = val;

        if (m_cb) {
            m_cb(val);
        }

        if (changed) {
            m_ev.notify();
        }
    }
};

template <>
class TargetSignalSocketProxy<bool> : public sc_core::sc_signal_inout_if<bool>
{
public:
    using Iface = sc_core::sc_signal_inout_if<bool>;
    using ValueChangedCallback = std::function<void(const bool&)>;

protected:
    TargetSignalSocket<bool>& m_parent;

    bool m_val;
    ValueChangedCallback m_cb;
    gs::async_event m_ev;
    gs::async_event m_posedge_ev;
    gs::async_event m_negedge_ev;

public:
    TargetSignalSocketProxy<bool>(TargetSignalSocket<bool>& parent)
        : m_parent(parent), m_val(false), m_cb(nullptr), m_ev(false), m_posedge_ev(false), m_negedge_ev(false)
    {
    }

    void register_value_changed_cb(const ValueChangedCallback& cb) { m_cb = cb; }

    TargetSignalSocket<bool>& get_parent() { return m_parent; }

    void notify() { m_ev.notify(); }

    /* SystemC interfaces */

    /* sc_core::sc_signal_in_if<bool> */
    virtual const sc_core::sc_event& default_event() const { return m_ev; }

    virtual const sc_core::sc_event& value_changed_event() const { return m_ev; }

    virtual const sc_core::sc_event& posedge_event() const { return m_posedge_ev; }

    virtual const sc_core::sc_event& negedge_event() const { return m_negedge_ev; }

    virtual const bool& read() const { return m_val; }

    virtual const bool& get_data_ref() const { return m_val; }

    virtual bool event() const
    {
        /* Not implemented */
        assert(false);
        return false;
    }

    virtual bool posedge() const
    {
        /* Not implemented */
        assert(false);
        return false;
    }

    virtual bool negedge() const
    {
        /* Not implemented */
        assert(false);
        return false;
    }

    /* sc_core::sc_signal_write_if<bool> */
    virtual void write(const bool& val)
    {
        bool changed = (val != m_val);
        m_val = val;

        if (m_cb) {
            m_cb(val);
        }

        if (changed) {
            m_ev.notify();
            val ? m_posedge_ev.notify() : m_negedge_ev.notify();
        }
    }
};

/* TODO: TargetSignalSocketProxy specialization for sc_dt::sc_logic type */

template <class T>
class TargetSignalSocket : public sc_core::sc_export<sc_core::sc_signal_inout_if<T> >
{
public:
    using Iface = typename TargetSignalSocketProxy<T>::Iface;
    using Parent = sc_core::sc_export<Iface>;
    using ValueChangedCallback = typename TargetSignalSocketProxy<T>::ValueChangedCallback;

protected:
    TargetSignalSocketProxy<T> m_proxy;

public:
    TargetSignalSocket(const char* name): Parent(name), m_proxy(*this) { Parent::bind(m_proxy); }

    void register_value_changed_cb(const ValueChangedCallback& cb) { m_proxy.register_value_changed_cb(cb); }

    const T& read() const { return m_proxy.read(); }
};

#endif
