/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __GS_FSS_UTILS_H__
#define __GS_FSS_UTILS_H__

#include <systemc>
#include <tlm>
#include <cstddef>
#include <new>
#include <string>
#include <cassert>
#include <type_traits>
#include <utility>
#include "fss_interfaces.h"

namespace gs {

/**
 * Get the pointer of PARENT_TYPE instance containing the MEMBER_TYPE member using a pointer to this member.
 * Args:
 * member: pointer of type MEMBER_TYPE* to the member.
 * member_ptr: pointer of type PARENT_TYPE::* to the specific member of the PARENT_TYPE.
 * Ret:
 * pointer to the parent instance of type PARENT_TYPE contaning the member pointer.
 */
template <class PARENT_TYPE, class MEMBER_TYPE>
PARENT_TYPE* container_of(const MEMBER_TYPE* member, const MEMBER_TYPE PARENT_TYPE::* member_ptr)
{
    const PARENT_TYPE* const parent = 0;
    auto member_offset = reinterpret_cast<ptrdiff_t>(reinterpret_cast<const uint8_t*>(&(parent->*member_ptr)) -
                                                     reinterpret_cast<const uint8_t*>(parent));
    return std::launder(
        reinterpret_cast<PARENT_TYPE*>(reinterpret_cast<size_t>(member) - static_cast<size_t>(member_offset)));
}

void die_null_ptr(const std::string& fn_name, const std::string& ptr_name)
{
    std::cerr << "Error: fss_utils.h -> Function: " << fn_name << " => pointer: " << ptr_name << "is null!"
              << std::endl;
    exit(EXIT_FAILURE);
}

template <typename INTERFACE>
class fss_binder_if
{
public:
    virtual INTERFACE* get_if_handle() const = 0;

    virtual fssString get_if_name() const = 0;

    virtual void bind(fss_binder_if<INTERFACE>& other)
    {
        bind_if(other.get_if_name(), other.get_if_handle());
        other.bind_if(get_if_name(), get_if_handle());
    }

    virtual ~fss_binder_if() {}

protected:
    virtual void bind_if(fssString if_name, INTERFACE* if_handle) = 0;
};

template <typename T>
using BIND_IF_FN_PTR = fssRetStatus (*)(fssBindIFHandle, fssString, T*);

template <typename T>
using GET_IF_FN_PTR = T* (*)(fssBindIFHandle, fssString);

template <typename IF>
class fss_binder_base : public sc_core::sc_object, public fss_binder_if<IF>
{
public:
    fss_binder_base(fssString obj_name, fssString if_name, fssBindIFHandle bind_handle)
        : sc_core::sc_object(obj_name), m_if_name(if_name), m_bind_handle(bind_handle)
    {
        if (!bind_handle) {
            die_null_ptr("fss_binder_base", "bind_handle");
        }
        if (!if_name) {
            die_null_ptr("fss_binder_base", "if_name");
        }
        GET_IF_FN_PTR<IF> get_if;
        if constexpr (std::is_same_v<IF, fssEventsIf>) {
            get_if = bind_handle->getEventsIfHandle;
        } else if constexpr (std::is_same_v<IF, fssTimeSyncIf>) {
            get_if = bind_handle->getTimeSyncIfHandle;
        } else if constexpr (std::is_same_v<IF, fssControlIf>) {
            get_if = bind_handle->getControlIfHandle;
        } else {
            get_if = bind_handle->getBusIfHandle;
        }
        if (!get_if) {
            die_null_ptr("fss_binder_base", "get_if");
        }
        m_if_handle = get_if(bind_handle, if_name);
        if (!m_if_handle) {
            die_null_ptr("fss_binder_base", "m_if_handle");
        }
    }

    fss_binder_base(fssString obj_name, fssString _if_name, IF* if_handle)
        : sc_core::sc_object(obj_name), m_if_name(_if_name), m_if_handle(if_handle)
    {
        if (!if_handle) {
            die_null_ptr("fss_binder_base", "if_handle");
        }
        if (!_if_name) {
            die_null_ptr("fss_binder_base", "_if_name");
        }
        m_bind_handle = nullptr;
    }

    IF* get_if_handle() const override { return m_if_handle; }

    fssString get_if_name() const override { return m_if_name.c_str(); }

    virtual ~fss_binder_base() {}

protected:
    void bind_if(fssString if_name, IF* if_handle) override
    {
        if (!if_name) {
            die_null_ptr("bind_if", "if_name");
        }
        if (!if_handle) {
            die_null_ptr("bind_if", "if_handle");
        }
        other_if_handle = if_handle;
        if (m_bind_handle) {
            BIND_IF_FN_PTR<IF> bind_if_if;
            if constexpr (std::is_same_v<IF, fssEventsIf>) {
                bind_if_if = m_bind_handle->bindEventsIf;
            } else if constexpr (std::is_same_v<IF, fssTimeSyncIf>) {
                bind_if_if = m_bind_handle->bindTimeSyncIf;
            } else if constexpr (std::is_same_v<IF, fssControlIf>) {
                bind_if_if = m_bind_handle->bindControlIf;
            } else {
                bind_if_if = m_bind_handle->bindBusIf;
            }
            if (!bind_if_if) {
                die_null_ptr("bind_if", "bind_if_if");
            }
            bind_if_if(m_bind_handle, if_name, if_handle);
        }
    }

protected:
    std::string m_if_name;
    fssBindIFHandle m_bind_handle;
    IF* m_if_handle;
    IF* other_if_handle;
};

class fss_events_binder : public fss_binder_base<fssEventsIf>
{
public:
    fss_events_binder(fssString obj_name, fssString events_if_name, fssBindIFHandle bind_handle)
        : fss_binder_base<fssEventsIf>(obj_name, events_if_name, bind_handle)
    {
    }

    fss_events_binder(fssString obj_name, fssString events_if_name, fssEventsIfHandle events_if_handle)
        : fss_binder_base<fssEventsIf>(obj_name, events_if_name, events_if_handle)
    {
    }

    void notify(fssUint64 event)
    {
        if (!other_if_handle) {
            die_null_ptr("bind_if", "other_if_handle");
        }
        if (!other_if_handle->handleEvents) {
            die_null_ptr("bind_if", "other_if_handle->handleEvents");
        }
        other_if_handle->handleEvents(other_if_handle, event);
    }

    virtual ~fss_events_binder() {}
};

class fss_time_sync_binder : public fss_binder_base<fssTimeSyncIf>
{
public:
    fss_time_sync_binder(fssString obj_name, fssString time_sync_if_name, fssBindIFHandle bind_handle)
        : fss_binder_base<fssTimeSyncIf>(obj_name, time_sync_if_name, bind_handle)
    {
    }

    fss_time_sync_binder(fssString obj_name, fssString time_sync_if_name, fssTimeSyncIfHandle time_sync_if_handle)
        : fss_binder_base<fssTimeSyncIf>(obj_name, time_sync_if_name, time_sync_if_handle)
    {
    }

    void update_time(const TimeWindow& window)
    {
        if (!other_if_handle) {
            die_null_ptr("update_time", "other_if_handle");
        }
        if (!other_if_handle->updateTimeWindow) {
            die_null_ptr("update_time", "other_if_handle->updateTimeWindow");
        }
        other_if_handle->updateTimeWindow(other_if_handle, window);
    }

    virtual ~fss_time_sync_binder() {}
};

class fss_data_binder : public fss_binder_base<fssBusIf>
{
public:
    fss_data_binder(fssString obj_name, fssString data_if_name, fssBindIFHandle bind_handle)
        : fss_binder_base<fssBusIf>(obj_name, data_if_name, bind_handle)
    {
    }

    fss_data_binder(fssString obj_name, fssString data_if_name, fssBusIfHandle data_if_handle)
        : fss_binder_base<fssBusIf>(obj_name, data_if_name, data_if_handle)
    {
    }

    fssBusIfItemSize get_number()
    {
        if (!other_if_handle) {
            die_null_ptr("get_number", "other_if_handle");
        }
        if (!other_if_handle->getNumber) {
            die_null_ptr("get_number", "other_if_handle->getNumber");
        }
        return other_if_handle->getNumber(other_if_handle);
    }
    fssString get_name(fssBusIfItemIndex _index)
    {
        if (!other_if_handle) {
            die_null_ptr("get_name", "other_if_handle");
        }
        if (!other_if_handle->getName) {
            die_null_ptr("get_name", "other_if_handle->getName");
        }
        return other_if_handle->getName(other_if_handle, _index);
    }
    fssBusIfItemSize get_size(fssBusIfItemIndex _index)
    {
        if (!other_if_handle) {
            die_null_ptr("get_size", "other_if_handle");
        }
        if (!other_if_handle->getSize) {
            die_null_ptr("get_size", "other_if_handle->getSize");
        }
        return other_if_handle->getSize(other_if_handle, _index);
    }
    fssBusIfItemSize get_index(fssString _name)
    {
        if (!other_if_handle) {
            die_null_ptr("get_index", "other_if_handle");
        }
        if (!other_if_handle->getIndex) {
            die_null_ptr("get_index", "other_if_handle->getIndex");
        }
        return other_if_handle->getIndex(other_if_handle, _name);
    }
    fssBusIfItemIndex add_item(fssString _name, fssBusIfItemSize _size)
    {
        if (!other_if_handle) {
            die_null_ptr("add_item", "other_if_handle");
        }
        if (!other_if_handle->addItem) {
            die_null_ptr("add_item", "other_if_handle->addItem");
        }
        return other_if_handle->addItem(other_if_handle, _name, _size);
    }
    fssBusIfItemData get_item(fssBusIfItemIndex _index)
    {
        if (!other_if_handle) {
            die_null_ptr("get_item", "other_if_handle");
        }
        if (!other_if_handle->getItem) {
            die_null_ptr("get_item", "other_if_handle->getItem");
        }
        return other_if_handle->getItem(other_if_handle, _index);
    }
    void set_item(fssBusIfItemIndex _index, fssBusIfItemData _data)
    {
        if (!other_if_handle) {
            die_null_ptr("set_item", "other_if_handle");
        }
        if (!other_if_handle->setItem) {
            die_null_ptr("set_item", "other_if_handle->setItem");
        }
        other_if_handle->setItem(other_if_handle, _index, _data);
    }

    void transmit()
    {
        if (!other_if_handle) {
            die_null_ptr("transmit", "other_if_handle");
        }
        if (!other_if_handle->transmit) {
            die_null_ptr("transmit", "other_if_handle->transmit");
        }
        other_if_handle->transmit(other_if_handle);
    }

    virtual ~fss_data_binder() {}
};

class fss_control_binder : public fss_binder_base<fssControlIf>
{
public:
    fss_control_binder(fssString obj_name, fssString control_if_name, fssBindIFHandle bind_handle)
        : fss_binder_base<fssControlIf>(obj_name, control_if_name, bind_handle)
    {
    }

    fss_control_binder(fssString obj_name, fssString control_if_name, fssControlIfHanlde control_if_handle)
        : fss_binder_base<fssControlIf>(obj_name, control_if_name, control_if_handle)
    {
    }

    fssString do_command(fssString cmd)
    {
        if (!other_if_handle) {
            die_null_ptr("do_command", "other_if_handle");
        }
        if (!other_if_handle->doCommand) {
            die_null_ptr("do_command", "other_if_handle->doCommand");
        }
        return other_if_handle->doCommand(other_if_handle, cmd);
    }

    virtual ~fss_control_binder() {}
};

} // namespace gs

#endif
