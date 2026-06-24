/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _SMMUV3_EXTENSIONS_H_
#define _SMMUV3_EXTENSIONS_H_

#include <tlm>
#include <cstdint>

namespace gs {

// SS = SubStream: an upstream device may tag a transaction with a sub-channel id (similar to a process/address-space
// tag)
class smmuv3_ss_extension : public tlm::tlm_extension<smmuv3_ss_extension>
{
public:
    uint32_t substream_id = 0; // Sub-channel id within a stream; selects one Context Descriptor under a given StreamID
    bool ssv = false;          // SSV = SubStream Valid: tells the SMMU that substream_id above is meaningful

    tlm::tlm_extension_base* clone() const override { return new smmuv3_ss_extension(*this); }
    void copy_from(const tlm::tlm_extension_base& ext) override
    {
        const auto& other = static_cast<const smmuv3_ss_extension&>(ext);
        substream_id = other.substream_id;
        ssv = other.ssv;
    }
};

class smmuv3_secure_extension : public tlm::tlm_extension<smmuv3_secure_extension>
{
public:
    bool secure = false;

    tlm::tlm_extension_base* clone() const override { return new smmuv3_secure_extension(*this); }
    void copy_from(const tlm::tlm_extension_base& ext) override
    {
        const auto& other = static_cast<const smmuv3_secure_extension&>(ext);
        secure = other.secure;
    }
};

// ATS = Address Translation Service: an upstream device asks the SMMU to translate an address
//       (and cache the result locally) instead of having the SMMU translate every transaction.
class smmuv3_ats_extension : public tlm::tlm_extension<smmuv3_ats_extension>
{
public:
    bool is_translation_request = false; // True for an ATS query (translate this VA, do not actually access memory)
    bool is_translated = false;          // True when the upstream device is presenting an already-translated PA
    uint32_t prg_index = 0; // PRG = Page Request Group: id used to match a page-fault response to its request

    uint64_t pa = 0;          // PA = Physical Address: the SMMU fills this in with the translation result
    uint8_t granule_log2 = 0; // log2 of page size of the result (e.g. 12 = 4 KiB)
    uint8_t prot = 0;         // Read/Write permissions encoded into the IOMMUAccessFlags scheme
    uint8_t fault_type = 0;   // Reason code if the translation failed
    bool faulted = false;     // True if the SMMU could not translate (see fault_type)

    tlm::tlm_extension_base* clone() const override { return new smmuv3_ats_extension(*this); }
    void copy_from(const tlm::tlm_extension_base& ext) override
    {
        const auto& other = static_cast<const smmuv3_ats_extension&>(ext);
        is_translation_request = other.is_translation_request;
        is_translated = other.is_translated;
        prg_index = other.prg_index;
        pa = other.pa;
        granule_log2 = other.granule_log2;
        prot = other.prot;
        fault_type = other.fault_type;
        faulted = other.faulted;
    }
};

} // namespace gs

#endif
