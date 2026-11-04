/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _LIBQBOX_TLM_EXTENSIONS_QEMU_MEMTX_ATTRS_H
#define _LIBQBOX_TLM_EXTENSIONS_QEMU_MEMTX_ATTRS_H

#include <cstdint>

#include <tlm>

namespace gs {

enum class QemuSecuritySpace : uint8_t {
    Secure = 0,
    NonSecure = 1,
    Root = 2,
    Realm = 3,
};

template <typename Tag, typename Value>
class QemuMemTxAttrTlmExtension : public tlm::tlm_extension<QemuMemTxAttrTlmExtension<Tag, Value>>
{
public:
    Value value{};

    QemuMemTxAttrTlmExtension() = default;
    QemuMemTxAttrTlmExtension(const QemuMemTxAttrTlmExtension&) = default;

    tlm::tlm_extension_base* clone() const override { return new QemuMemTxAttrTlmExtension(*this); }

    void copy_from(const tlm::tlm_extension_base& ext) override
    {
        *this = static_cast<const QemuMemTxAttrTlmExtension&>(ext);
    }
};

struct QemuMemTxSecureAttrTag;
struct QemuMemTxSpaceAttrTag;
struct QemuMemTxUserAttrTag;
struct QemuMemTxMemoryAttrTag;
struct QemuMemTxDebugAttrTag;
struct QemuMemTxRequesterIdAttrTag;
struct QemuMemTxPidAttrTag;
struct QemuMemTxAddressTypeAttrTag;
struct QemuMemTxUnspecifiedAttrTag;

using QemuMemTxSecureTlmExtension = QemuMemTxAttrTlmExtension<QemuMemTxSecureAttrTag, bool>;
using QemuMemTxSpaceTlmExtension = QemuMemTxAttrTlmExtension<QemuMemTxSpaceAttrTag, QemuSecuritySpace>;
using QemuMemTxUserTlmExtension = QemuMemTxAttrTlmExtension<QemuMemTxUserAttrTag, bool>;
using QemuMemTxMemoryTlmExtension = QemuMemTxAttrTlmExtension<QemuMemTxMemoryAttrTag, bool>;
using QemuMemTxDebugTlmExtension = QemuMemTxAttrTlmExtension<QemuMemTxDebugAttrTag, bool>;
using QemuMemTxRequesterIdTlmExtension = QemuMemTxAttrTlmExtension<QemuMemTxRequesterIdAttrTag, uint16_t>;
using QemuMemTxPidTlmExtension = QemuMemTxAttrTlmExtension<QemuMemTxPidAttrTag, uint8_t>;
using QemuMemTxAddressTypeTlmExtension = QemuMemTxAttrTlmExtension<QemuMemTxAddressTypeAttrTag, bool>;
using QemuMemTxUnspecifiedTlmExtension = QemuMemTxAttrTlmExtension<QemuMemTxUnspecifiedAttrTag, bool>;

} // namespace gs

#endif
