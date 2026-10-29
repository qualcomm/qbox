/*
 * This file is part of libqbox
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_TLM_EXTENSIONS_QEMU_MEMTX_ATTR_EXTENSIONS_H
#define _LIBQBOX_TLM_EXTENSIONS_QEMU_MEMTX_ATTR_EXTENSIONS_H

#include <tlm>

#include <libqemu-cxx/libqemu-cxx.h>

#include <tlm-extensions/qemu-memtx-attrs.h>

struct QemuMemTxAttrExtensions {
    gs::QemuMemTxSecureTlmExtension secure;
    gs::QemuMemTxSpaceTlmExtension space;
    gs::QemuMemTxUserTlmExtension user;
    gs::QemuMemTxMemoryTlmExtension memory;
    gs::QemuMemTxDebugTlmExtension debug;
    gs::QemuMemTxRequesterIdTlmExtension requester_id;
    gs::QemuMemTxPidTlmExtension pid;
    gs::QemuMemTxAddressTypeTlmExtension address_type;
    gs::QemuMemTxUnspecifiedTlmExtension unspecified;
    tlm::tlm_generic_payload& txn;

    QemuMemTxAttrExtensions(tlm::tlm_generic_payload& txn, qemu::MemoryRegionOps::MemTxAttrs attrs): txn(txn)
    {
        secure.value = attrs.secure;
        space.value = static_cast<gs::QemuSecuritySpace>(attrs.space);
        user.value = attrs.user;
        memory.value = attrs.memory;
        debug.value = attrs.debug;
        requester_id.value = attrs.requester_id;
        pid.value = attrs.pid;
        address_type.value = attrs.address_type;
        unspecified.value = attrs.unspecified;
        txn.set_extension(&secure);
        txn.set_extension(&space);
        txn.set_extension(&user);
        txn.set_extension(&memory);
        txn.set_extension(&debug);
        txn.set_extension(&requester_id);
        txn.set_extension(&pid);
        txn.set_extension(&address_type);
        txn.set_extension(&unspecified);
    }

    ~QemuMemTxAttrExtensions()
    {
        txn.clear_extension(&secure);
        txn.clear_extension(&space);
        txn.clear_extension(&user);
        txn.clear_extension(&memory);
        txn.clear_extension(&debug);
        txn.clear_extension(&requester_id);
        txn.clear_extension(&pid);
        txn.clear_extension(&address_type);
        txn.clear_extension(&unspecified);
    }
};

#endif
