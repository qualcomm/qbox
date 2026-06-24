/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef SMMUV3_MEMORY_ATTRS_H
#define SMMUV3_MEMORY_ATTRS_H

#include <tlm>
#include <sstream>
#include <cstdint>

namespace gs {

// Carries page-table memory attributes (cacheability, shareability, permissions) downstream
// alongside a translated transaction so the rest of the system can honour them.
class smmuv3_memory_attrs_extension : public tlm::tlm_extension<smmuv3_memory_attrs_extension>
{
public:
    // Memory type selects how the interconnect/caches are allowed to treat an access.
    // Naming follows the page-table encoding: "n" = "non", G/R/E = Gathering/Reordering/Early-write-ack;
    // so DEVICE_nGnRnE is the strictest device type, NORMAL_WB is fully cacheable write-back.
    enum class MemoryType : uint8_t {
        DEVICE_nGnRnE = 0, // device, no Gathering, no Reordering, no Early write acknowledge
        DEVICE_nGnRE = 1,  // device, no Gathering, no Reordering, Early write ack allowed
        DEVICE_nGRE = 2,   // device, no Gathering, Reordering allowed, Early write ack allowed
        DEVICE_GRE = 3,    // device, Gathering/Reordering/Early-ack all allowed (loosest device)
        NORMAL_NC = 4,     // normal memory, Non-Cacheable
        NORMAL_WT = 5,     // normal memory, Write-Through cacheable
        NORMAL_WB = 6,     // normal memory, Write-Back cacheable (the default)
    };

    // How widely a normal-memory access must be observed (which cache scope must agree on its value).
    enum class Shareability : uint8_t {
        NON_SHAREABLE = 0,
        RESERVED = 1,
        OUTER_SHAREABLE = 2, // visible to "outer" sharers (typically across clusters/system)
        INNER_SHAREABLE = 3, // visible to "inner" sharers (typically within a cluster)
    };

    // AccessPerm = Access Permission: read/write rights for privileged (kernel) vs unprivileged (user) accessors.
    // RW = Read+Write, RO = Read-Only, NONE = no access.
    enum class AccessPerm : uint8_t {
        PRIV_RW_USER_NONE = 0,
        PRIV_RW_USER_RW = 1,
        PRIV_RO_USER_NONE = 2,
        PRIV_RO_USER_RO = 3,
    };

    smmuv3_memory_attrs_extension()
        : m_memory_type(MemoryType::NORMAL_WB)
        , m_shareability(Shareability::INNER_SHAREABLE)
        , m_access_perm(AccessPerm::PRIV_RW_USER_RW)
        , m_attr_indx(0)
        , m_non_secure(true)
        , m_privileged_execute_never(false)
        , m_unprivileged_execute_never(false)
        , m_contiguous(false)
        , m_dirty_bit_modifier(false)
    {
    }

    virtual tlm::tlm_extension_base* clone() const override
    {
        auto* ext = new smmuv3_memory_attrs_extension();
        ext->m_memory_type = m_memory_type;
        ext->m_shareability = m_shareability;
        ext->m_access_perm = m_access_perm;
        ext->m_attr_indx = m_attr_indx;
        ext->m_non_secure = m_non_secure;
        ext->m_privileged_execute_never = m_privileged_execute_never;
        ext->m_unprivileged_execute_never = m_unprivileged_execute_never;
        ext->m_contiguous = m_contiguous;
        ext->m_dirty_bit_modifier = m_dirty_bit_modifier;
        return ext;
    }

    virtual void copy_from(const tlm::tlm_extension_base& ext) override
    {
        const auto& other = static_cast<const smmuv3_memory_attrs_extension&>(ext);
        m_memory_type = other.m_memory_type;
        m_shareability = other.m_shareability;
        m_access_perm = other.m_access_perm;
        m_attr_indx = other.m_attr_indx;
        m_non_secure = other.m_non_secure;
        m_privileged_execute_never = other.m_privileged_execute_never;
        m_unprivileged_execute_never = other.m_unprivileged_execute_never;
        m_contiguous = other.m_contiguous;
        m_dirty_bit_modifier = other.m_dirty_bit_modifier;
    }

    void set_memory_type(MemoryType type) { m_memory_type = type; }
    void set_shareability(Shareability sh) { m_shareability = sh; }
    void set_access_perm(AccessPerm ap) { m_access_perm = ap; }
    void set_attr_indx(uint8_t idx) { m_attr_indx = idx; }
    void set_non_secure(bool ns) { m_non_secure = ns; }
    void set_pxn(bool pxn) { m_privileged_execute_never = pxn; }
    void set_uxn(bool uxn) { m_unprivileged_execute_never = uxn; }
    void set_contiguous(bool c) { m_contiguous = c; }
    void set_dbm(bool dbm) { m_dirty_bit_modifier = dbm; }

    MemoryType get_memory_type() const { return m_memory_type; }
    Shareability get_shareability() const { return m_shareability; }
    AccessPerm get_access_perm() const { return m_access_perm; }
    uint8_t get_attr_indx() const { return m_attr_indx; }
    bool is_non_secure() const { return m_non_secure; }
    bool is_pxn() const { return m_privileged_execute_never; }
    bool is_uxn() const { return m_unprivileged_execute_never; }
    bool is_contiguous() const { return m_contiguous; }
    bool is_dbm() const { return m_dirty_bit_modifier; }

    void set_from_descriptor(uint64_t desc, uint32_t table_attrs = 0)
    {
        m_attr_indx = (desc >> 2) & 0xF;
        m_shareability = static_cast<Shareability>((desc >> 8) & 0x3);
        m_access_perm = static_cast<AccessPerm>((desc >> 6) & 0x3);
        m_non_secure = (desc >> 5) & 0x1;
        m_privileged_execute_never = ((desc >> 53) & 0x1) || ((table_attrs >> 3) & 0x1);
        m_unprivileged_execute_never = ((desc >> 54) & 0x1) || ((table_attrs >> 4) & 0x1);
        m_contiguous = (desc >> 52) & 0x1;
        m_dirty_bit_modifier = (desc >> 51) & 0x1;
        m_memory_type = MemoryType::NORMAL_WB;
    }

    std::string to_string() const
    {
        std::ostringstream oss;
        oss << "MemAttrs{type=";
        switch (m_memory_type) {
        case MemoryType::DEVICE_nGnRnE:
            oss << "Device-nGnRnE";
            break;
        case MemoryType::DEVICE_nGnRE:
            oss << "Device-nGnRE";
            break;
        case MemoryType::DEVICE_nGRE:
            oss << "Device-nGRE";
            break;
        case MemoryType::DEVICE_GRE:
            oss << "Device-GRE";
            break;
        case MemoryType::NORMAL_NC:
            oss << "Normal-NC";
            break;
        case MemoryType::NORMAL_WT:
            oss << "Normal-WT";
            break;
        case MemoryType::NORMAL_WB:
            oss << "Normal-WB";
            break;
        default:
            oss << "Unknown";
            break;
        }
        oss << ", sh=";
        switch (m_shareability) {
        case Shareability::NON_SHAREABLE:
            oss << "Non";
            break;
        case Shareability::OUTER_SHAREABLE:
            oss << "Outer";
            break;
        case Shareability::INNER_SHAREABLE:
            oss << "Inner";
            break;
        default:
            oss << "Reserved";
            break;
        }
        oss << ", ap=" << static_cast<int>(m_access_perm) << ", ns=" << m_non_secure
            << ", pxn=" << m_privileged_execute_never << ", uxn=" << m_unprivileged_execute_never << "}";
        return oss.str();
    }

private:
    MemoryType m_memory_type;
    Shareability m_shareability;
    AccessPerm m_access_perm;
    uint8_t m_attr_indx;               // attr_indx = index into a CPU-side memory-attribute lookup table (MAIR-style)
    bool m_non_secure;                 // NS = Non-Secure: when true the access is in the Non-Secure world
    bool m_privileged_execute_never;   // PXN = Privileged eXecute Never: kernel cannot fetch instructions here
    bool m_unprivileged_execute_never; // UXN = Unprivileged eXecute Never: user cannot fetch instructions here
    bool m_contiguous;                 // page-table "contiguous" hint: this entry is part of a run of pages
    bool m_dirty_bit_modifier;         // DBM = Dirty Bit Modifier: lets hardware mark the page dirty on write
};

} // namespace gs

#endif
