/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef SMMUV3_H
#define SMMUV3_H
namespace gs {
const void* smmuv3_zip_archive_data() noexcept;
unsigned int smmuv3_zip_archive_size() noexcept;
} // namespace gs

#include <systemc>
#include <cci_configuration>
#include <cciutils.h>
#include <scp/report.h>
#include <tlm>
#include <module_factory_registery.h>
#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <tlm-extensions/underlying-dmi.h>
#include <registers.h>
#include <reg_model_maker/reg_model_maker.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include <unordered_map>
#include <list>

#include "smmuv3_gen.h"
#include "smmuv3_memory_attrs.h"
#include "smmuv3_extensions.h"

namespace gs {

constexpr uint64_t SMMUV3_PAGESIZE = 4096;
constexpr uint64_t SMMUV3_PAGEMASK = SMMUV3_PAGESIZE - 1;
constexpr uint32_t
    SMMUV3_MAX_TBU = 16; // TBU = Translation Buffer Unit: per-master front-end that owns one upstream port

// Per-record sizes for the structures the SMMU walks/produces in memory.
constexpr size_t SMMUV3_STE_SIZE = 64; // STE = Stream Table Entry (one per upstream device, indexed by StreamID)
constexpr size_t
    SMMUV3_CD_SIZE = 64; // CD  = Context Descriptor (one per address space behind a device, holds page-table roots)
constexpr size_t SMMUV3_CMD_SIZE = 16;   // command-queue entry size
constexpr size_t SMMUV3_EVENT_SIZE = 32; // event-queue entry size
constexpr size_t SMMUV3_PRI_SIZE = 16;   // PRI = Page Request Interface queue entry size

// EVT_* = EVenT type codes the SMMU writes into the event queue when something happens.
// Prefix F_ = Fault (translation-time error against a transaction);
// Prefix C_ = Configuration error (bad table contents the driver gave us).
constexpr uint32_t SMMUV3_EVT_F_UUT = 0x01;          // UUT = Unsupported Upstream Transaction
constexpr uint32_t SMMUV3_EVT_C_BAD_STREAMID = 0x02; // transaction came in with a StreamID outside the configured table
constexpr uint32_t SMMUV3_EVT_F_STE_FETCH = 0x03;    // bus error while reading the Stream Table Entry
constexpr uint32_t SMMUV3_EVT_C_BAD_STE = 0x04;      // Stream Table Entry contents are malformed
constexpr uint32_t SMMUV3_EVT_C_BAD_SUBSTREAMID = 0x08; // SubstreamID is outside the range the STE allows
constexpr uint32_t SMMUV3_EVT_F_CD_FETCH = 0x09;        // bus error while reading the Context Descriptor
constexpr uint32_t SMMUV3_EVT_C_BAD_CD = 0x0A;          // Context Descriptor contents are malformed
constexpr uint32_t SMMUV3_EVT_F_WALK_EABT = 0x0B;       // EABT = External AborT: bus error during page-table walk
constexpr uint32_t SMMUV3_EVT_F_TRANSLATION = 0x10;     // VA had no valid mapping
constexpr uint32_t SMMUV3_EVT_F_ADDR_SIZE = 0x11;       // an address (input or output) didn't fit the configured size
constexpr uint32_t SMMUV3_EVT_F_ACCESS = 0x12;          // Access-Flag was clear and HW couldn't set it
constexpr uint32_t SMMUV3_EVT_F_PERMISSION = 0x13;      // page exists but read/write permission was denied
constexpr uint32_t SMMUV3_EVT_F_STALL = 0x29; // synthetic event used when a fault is being stalled (driver must resume)

// CERROR = Command-queue ERROR codes the SMMU writes back to CMDQ_CONS.ERR when it rejects a command.
constexpr uint8_t SMMUV3_CERROR_NONE = 0;         // no error
constexpr uint8_t SMMUV3_CERROR_ILL = 1;          // ILL = ILLegal/unrecognised opcode
constexpr uint8_t SMMUV3_CERROR_ABT = 2;          // ABT = bus AborT while fetching the command word from memory
constexpr uint8_t SMMUV3_CERROR_ATC_INV_SYNC = 3; // ATC = Address Translation Cache: invalidation sync timed out

// CLASS = which subsystem caused the fault (used in event records).
constexpr uint32_t SMMUV3_CLASS_TT = 0; // TT = Translation Table walk
constexpr uint32_t SMMUV3_CLASS_IN = 1; // IN = INput-side (e.g. stage-2 fault on an input address)
constexpr uint32_t SMMUV3_CLASS_CD = 2; // CD = Context-Descriptor fetch

// EVT_W0_*/EVT_W1_* = bit positions/masks within Word 0 and Word 1 of an event-queue entry.
constexpr uint32_t EVT_W0_TYPE_MASK = 0xFFu;
constexpr uint32_t EVT_W0_SSV_BIT = 12;    // SSV = SubStream Valid (the SSID field below is meaningful)
constexpr uint32_t EVT_W0_SSID_SHIFT = 13; // SSID = SubStream ID (sub-context within a StreamID)
constexpr uint32_t EVT_W0_SSID_MASK = 0xFFFFFu;
constexpr uint32_t EVT_W0_STREAMID_SHIFT = 32;
constexpr uint32_t
    EVT_W1_STAG_MASK = 0xFFFFu;         // STAG = Stall TAG: handle the driver uses to resume a stalled transaction
constexpr uint32_t EVT_W1_RNW_BIT = 17; // RNW = Read-Not-Write (1 = read, 0 = write)
constexpr uint32_t EVT_W1_CLASS_SHIFT = 20;
constexpr uint32_t EVT_W1_CLASS_MASK = 0x3u;
constexpr uint32_t EVT_W1_STALL_BIT = 23;    // 1 if this fault was delivered as stallable (driver must RESUME)
constexpr uint32_t EVT_W1_REASON_SHIFT = 24; // REASON = page-table level / sub-reason of the fault
constexpr uint32_t EVT_W1_REASON_MASK = 0x7u;

// GERROR_BIT_* = bit positions inside the GERROR (Global Error) register. MSI variants flag a failure
// to deliver the interrupt itself; non-MSI ones flag the underlying queue/error condition.
constexpr uint32_t GERROR_BIT_MSI_CMDQ_ABT = 4;
constexpr uint32_t GERROR_BIT_MSI_EVENTQ_ABT = 5;
constexpr uint32_t GERROR_BIT_MSI_PRIQ_ABT = 6;
constexpr uint32_t GERROR_BIT_MSI_GERROR_ABT = 7;

// S1FMT = Stage-1 stream-table FMT (format) encodings: how the per-StreamID Context Descriptor table is laid out.
constexpr uint32_t S1FMT_LINEAR = 0;        // single flat array of CDs indexed by SubstreamID
constexpr uint32_t S1FMT_4KB_L2 = 1;        // two-level table whose leaf pages are 4 KiB
constexpr uint32_t S1FMT_64KB_L2 = 2;       // two-level table whose leaf pages are 64 KiB
constexpr uint32_t S1FMT_4KB_L2_SPLIT = 4;  // SPLIT = how many low SubstreamID bits index the L2 (4 KiB case)
constexpr uint32_t S1FMT_64KB_L2_SPLIT = 8; // SPLIT for the 64 KiB-leaf case

// DESC_BIT_* = bit positions inside a 64-bit page-table DESCriptor word.
constexpr uint32_t DESC_BIT_AF = 10; // AF = Access Flag: HW sets this on first use of a page
constexpr uint32_t
    DESC_BIT_DBM = 51; // DBM = Dirty Bit Modifier: writeable page that should be marked dirty on first write

// load_le / store_le: little-endian (LE) read/write of an unaligned byte buffer. Stream-table entries,
// command-queue entries and so on are LE-packed in memory, and these helpers do the byte-safe conversion.
template <typename T>
static inline T load_le(const uint8_t* buf)
{
    T v{};
    std::memcpy(&v, buf, sizeof(T));
    return v;
}

template <typename T>
static inline void store_le(uint8_t* buf, T v)
{
    std::memcpy(buf, &v, sizeof(T));
}

static inline unsigned clamp_shift_u64(unsigned shift)
{
    constexpr unsigned MAX_SHIFT = 63;
    return shift > MAX_SHIFT ? MAX_SHIFT : shift;
}

static inline uint64_t safe_shl1_u64(unsigned shift) { return 1ULL << clamp_shift_u64(shift); }

static inline uint32_t clamp_queue_log2size(uint32_t log2size)
{
    constexpr uint32_t QUEUE_LOG2_MAX = 19;
    return log2size > QUEUE_LOG2_MAX ? QUEUE_LOG2_MAX : log2size;
}

// STE_CONFIG_* = decoded values of the STE.CONFIG field. They tell the SMMU what to do for this device.
constexpr uint32_t STE_CONFIG_ABORT = 0;  // refuse all traffic for this StreamID
constexpr uint32_t STE_CONFIG_BYPASS = 4; // pass through untranslated (1:1 VA = PA)
constexpr uint32_t STE_CONFIG_S1 = 5;     // S1 = Stage-1 only translation (per-process page tables)
constexpr uint32_t STE_CONFIG_S2 = 6;     // S2 = Stage-2 only translation (hypervisor page tables)
constexpr uint32_t STE_CONFIG_NESTED = 7; // NESTED = Stage-1 then Stage-2 (guest VA -> guest PA -> host PA)

// CD_TG_* = Translation Granule encoding inside a Context Descriptor (page size of S1 page tables).
constexpr uint8_t CD_TG_4K = 0;
constexpr uint8_t CD_TG_64K = 1;
constexpr uint8_t CD_TG_16K = 2;

// TLBI_TG_* = Translation Granule encoding used in TLBI (TLB Invalidate) commands. Note: this encoding
// differs from CD_TG_* — DC means "Default/Currently-active", and 4K/16K/64K reorder vs CD_TG_*.
constexpr uint8_t TLBI_TG_DC = 0;
constexpr uint8_t TLBI_TG_4K = 1;
constexpr uint8_t TLBI_TG_16K = 2;
constexpr uint8_t TLBI_TG_64K = 3;

// PAGE_SHIFT_* = log2 of the page size, i.e. how many low bits of an address are the offset within a page.
constexpr uint32_t PAGE_SHIFT_4K = 12;
constexpr uint32_t PAGE_SHIFT_16K = 14;
constexpr uint32_t PAGE_SHIFT_64K = 16;

// GRAINSIZE_* = log2 of the page-table "grain" (same numeric value as the page shift; "grain" is the table-walk term).
constexpr uint32_t GRAINSIZE_4K = 12;
constexpr uint32_t GRAINSIZE_16K = 14;
constexpr uint32_t GRAINSIZE_64K = 16;

// STRIDE_* = number of address bits resolved by one page-table level (i.e. log2 of entries-per-table).
constexpr uint32_t STRIDE_4K = 9;
constexpr uint32_t STRIDE_16K = 11;
constexpr uint32_t STRIDE_64K = 13;

// More GERROR (Global Error) bit positions; ABT = aBorT (bus error talking to that queue).
constexpr uint32_t GERROR_BIT_CMDQ_ERR = 0;
constexpr uint32_t GERROR_BIT_EVENTQ_ABT = 2;
constexpr uint32_t GERROR_BIT_PRIQ_ABT = 3;

constexpr uint32_t GERROR_BIT_SFM_ERR = 8; // SFM = Stream Fault Mode: a fatal stream-table walk error

// DESC_TYPE_* = the low 2 bits of a page-table descriptor say what kind of entry it is.
constexpr uint32_t DESC_TYPE_INVALID = 0; // entry is unused
constexpr uint32_t DESC_TYPE_BLOCK = 1;   // BLOCK = a large (multi-page) leaf mapping at this level
constexpr uint32_t DESC_TYPE_RESERVED = 2;
constexpr uint32_t DESC_TYPE_TABLE_OR_PAGE = 3; // mid-levels: pointer to next-level table; leaf level: a single page

// DESC_ATTR_* = bit positions inside the low-attribute byte of a descriptor.
constexpr uint32_t DESC_ATTR_AP1_BIT = 4; // AP1 = Access Permissions bit 1 (User access allowed)
constexpr uint32_t DESC_ATTR_AP2_BIT = 5; // AP2 = Access Permissions bit 2 (Read-only)
constexpr uint32_t DESC_ATTR_AF_BIT = 8;  // AF = Access Flag (page has been touched)

// S2AP = Stage-2 Access Permissions encoding (read/write rights granted by the hypervisor).
constexpr uint32_t S2AP_NONE = 0;
constexpr uint32_t S2AP_RO = 1; // RO = Read-Only
constexpr uint32_t S2AP_WO = 2; // WO = Write-Only
constexpr uint32_t S2AP_RW = 3; // RW = Read+Write

// Address-field masks used to strip control bits out of register values that hold a base address.
constexpr uint64_t STE_BASE_ADDR_MASK = 0x000FFFFFFFFFFFC0ULL; // Stream-table base / S1 context pointer
constexpr uint64_t S2TTB_ADDR_MASK = 0x000FFFFFFFFFFFF0ULL;    // S2TTB = Stage-2 Translation Table Base
constexpr uint64_t
    IPA_ADDR_MASK = 0x000FFFFFFFFFF000ULL; // IPA = Intermediate Physical Address (output of stage-1 / input to stage-2)
constexpr uint64_t CMDQ_BASE_HI_MASK = 0x000FFFFFULL;

// CMD_OP_* = OPcode byte that identifies a command-queue command.
//   PREFETCH_*  - hint commands; can be ignored.
//   CFGI_*      = ConFiGuration Invalidate: drop cached copies of stream-table / context-descriptor entries.
//   TLBI_*      = TLB Invalidate: drop cached translations.
//                 NH = Non-Hyp (EL1/EL0 stage-1 traffic), NS = Non-Secure, NSNH = Non-Secure Non-Hyp,
//                 EL2/EL3 = the corresponding privileged exception levels,
//                 S12 = both stages, S2 = stage-2 only,
//                 VA = by Virtual Address, VAA = by VA, All ASIDs, IPA = by Intermediate PA, ASID = by Address-Space
//                 ID, VMALL = all entries belonging to a VMID.
//   ATC_INV     = Address Translation Cache Invalidate (tells PCIe ATS clients to drop a cached translation).
//   PRI_RESP    = Page Request Interface RESPonse (driver answering a previously-queued PRI request).
//   RESUME      = release a stalled transaction (after the OS made the page available).
//   STALL_TERM  = TERMinate stall (kill the transaction with a fault).
//   SYNC        = barrier: all earlier commands complete before this one is acknowledged.
constexpr uint8_t CMD_OP_PREFETCH_CONFIG = 0x01;
constexpr uint8_t CMD_OP_PREFETCH_ADDR = 0x02;
constexpr uint8_t CMD_OP_CFGI_STE = 0x03;
constexpr uint8_t CMD_OP_CFGI_STE_RANGE = 0x04;
constexpr uint8_t CMD_OP_CFGI_CD = 0x05;
constexpr uint8_t CMD_OP_CFGI_CD_ALL = 0x06;
constexpr uint8_t CMD_OP_CFGI_ALL = 0x07;
constexpr uint8_t CMD_OP_TLBI_NH_ALL = 0x10;
constexpr uint8_t CMD_OP_TLBI_NH_ASID = 0x11;
constexpr uint8_t CMD_OP_TLBI_NH_VA = 0x12;
constexpr uint8_t CMD_OP_TLBI_NH_VAA = 0x13;
constexpr uint8_t CMD_OP_TLBI_EL3_ALL = 0x18;
constexpr uint8_t CMD_OP_TLBI_EL3_VA = 0x1A;
constexpr uint8_t CMD_OP_TLBI_EL2_ALL = 0x20;
constexpr uint8_t CMD_OP_TLBI_EL2_ASID = 0x21;
constexpr uint8_t CMD_OP_TLBI_EL2_VA = 0x22;
constexpr uint8_t CMD_OP_TLBI_EL2_VAA = 0x23;
constexpr uint8_t CMD_OP_TLBI_S12_VMALL = 0x28;
constexpr uint8_t CMD_OP_TLBI_S2_IPA = 0x2A;
constexpr uint8_t CMD_OP_TLBI_NSNH_ALL = 0x30;
constexpr uint8_t CMD_OP_ATC_INV = 0x40;
constexpr uint8_t CMD_OP_PRI_RESP = 0x41;
constexpr uint8_t CMD_OP_RESUME = 0x44;
constexpr uint8_t CMD_OP_STALL_TERM = 0x45;
constexpr uint8_t CMD_OP_SYNC = 0x46;

// SYNC_CS = Completion Signal field of the SYNC command: how the SMMU should notify the driver when the sync is done.
constexpr uint32_t SYNC_CS_NONE = 0; // no signal (driver will poll the consumer pointer)
constexpr uint32_t SYNC_CS_IRQ = 1;  // raise an interrupt (and optionally an MSI write)
constexpr uint32_t SYNC_CS_SEV = 2;  // SEV = Send EVent (SystemC: not modelled, treated as no-op)

constexpr uint32_t GATOS_PAR_FAULT_TRANSLATION = 0x10; // GATOS PAR fault-status code meaning "translation fault"

constexpr uint32_t IDR0_TTF_AARCH64 = 0x2;  // TTF (Translation Table Format) value advertising AArch64 page tables
constexpr uint32_t IDR0_STLEVEL_2LVL = 0x2; // Stream-table level value advertising the 2-level format
constexpr uint32_t IDR1_DEFAULT_QUEUE_LOG2 = 19;

// OAS = Output Address Size encoding (3-bit field, expands to a number of physical-address bits).
constexpr uint32_t OAS_32 = 0;
constexpr uint32_t OAS_36 = 1;
constexpr uint32_t OAS_40 = 2;
constexpr uint32_t OAS_42 = 3;
constexpr uint32_t OAS_44 = 4;
constexpr uint32_t OAS_48 = 5;

constexpr uint32_t PA_BITS = 48; // PA = Physical Address: largest physical address width this model supports
constexpr uint32_t
    VA_SIGN_BIT_SHIFT = 63; // VA = Virtual Address: bit 63 selects the upper/lower half of the address space
constexpr uint32_t IPA_PAGE_SHIFT = 12; // page shift used when treating an address as an IPA

constexpr uint32_t INPUT_SIZE_MIN = 25;
constexpr uint32_t INPUT_SIZE_MAX = 48;

// Translates an OAS/IPS (Intermediate Physical Size) 3-bit code into the actual number of address bits.
constexpr std::array<uint32_t, 8> OUTPUT_SIZE_MAP = { 32, 36, 40, 42, 44, 48, 48, 48 };

// Twelve bytes of fixed identification that appear in the top-of-page ID registers
// (vendor/part/revision/Class). These values are consumed by the IDREGS array; they are not arbitrary.
constexpr std::array<uint32_t, 12> PRIMECELL_IDS = {
    0x04, 0x00, 0x00, 0x00, 0x34, 0xB0, 0x0B, 0x00, 0x0D, 0xF0, 0x05, 0xB1,
};

// IOMMU = I/O Memory Management Unit (the role this whole module plays). AccessFlags describe what
// the translation grants downstream: RO = Read-Only, WO = Write-Only, RW = both, NONE = no access.
enum class IOMMUAccessFlags : uint8_t {
    NONE = 0,
    RO = 1,
    WO = 2,
    RW = 3,
};

constexpr IOMMUAccessFlags operator&(IOMMUAccessFlags a, IOMMUAccessFlags b)
{
    return static_cast<IOMMUAccessFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr IOMMUAccessFlags operator|(IOMMUAccessFlags a, IOMMUAccessFlags b)
{
    return static_cast<IOMMUAccessFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr IOMMUAccessFlags operator~(IOMMUAccessFlags a)
{
    return static_cast<IOMMUAccessFlags>(~static_cast<uint8_t>(a) & 0x3u);
}

inline IOMMUAccessFlags& operator&=(IOMMUAccessFlags& a, IOMMUAccessFlags b)
{
    a = a & b;
    return a;
}

inline IOMMUAccessFlags& operator|=(IOMMUAccessFlags& a, IOMMUAccessFlags b)
{
    a = a | b;
    return a;
}

template <unsigned int BUSWIDTH = 32>
class smmuv3_tbu;

// SMMUv3 = System Memory Management Unit, version 3. The "core" of the model: holds the
// register file (smmuv3_gen), translates transactions on behalf of one or more TBUs, and
// runs the command/event/PRI queues that the OS driver talks to.
template <unsigned int BUSWIDTH = 32>
class smmuv3 : public sc_core::sc_module, public smmuv3_gen
{
    SCP_LOGGER();
    cci::cci_broker_handle m_broker;
    gs::json_zip_archive m_jza;
    bool m_loaded_ok;
    gs::json_module M;

public:
    // IOMMUTLBEntry = the result of one translation. "TLB" (Translation Lookaside Buffer) is the
    // historical CPU-side name for the cached-translation structure; here it just means "one mapping".
    struct IOMMUTLBEntry {
        uint64_t iova;            // IOVA = I/O Virtual Address: the address the upstream device used
        uint64_t translated_addr; // resulting physical address (page-aligned)
        uint64_t addr_mask;       // low-bit mask of the page (page_size - 1); used to add the offset back in
        IOMMUAccessFlags perm;    // permissions granted by this translation (RO/WO/RW/NONE)
        uint64_t descriptor;      // raw leaf-level page-table descriptor (kept so memory attributes can be re-derived)
        uint32_t table_attrs;     // accumulated table-walk attributes (PXN/UXN/AP propagated from intermediate levels)
        uint32_t fault_type;      // fault reason code if perm == NONE (one of SMMUV3_EVT_*)
        uint32_t fault_level;     // page-table level at which the fault occurred
        bool stallable_fault;     // true if this fault should be reported as stallable (driver may RESUME)
    };

    // Configuration parameters (CCI = Configuration & Communication Interface — the SystemC parameter system):
    cci::cci_param<uint32_t> p_pamax;      // PAMAX = max Physical Address bits this instance advertises (32..48)
    cci::cci_param<uint16_t> p_sidsize;    // SIDSIZE = number of StreamID bits this instance accepts
    cci::cci_param<bool> p_ato;            // ATO = Address Translation Operations: enable the GATOS debug channel
    cci::cci_param<uint8_t> p_num_tbu;     // number of Translation Buffer Units (upstream front-ends) to expose
    cci::cci_param<uint32_t> p_iotlb_size; // IOTLB = I/O TLB: max number of cached translations to keep
    cci::cci_param<uint32_t> p_iidr;       // IIDR = Implementation ID Register value to advertise

    tlm_utils::multi_passthrough_target_socket<smmuv3, BUSWIDTH>
        socket; // MMIO target: register-file accesses arrive here
    tlm_utils::simple_initiator_socket<smmuv3>
        dma_socket; // DMA = Direct Memory Access port; SMMU uses it for table walks and queue accesses
    InitiatorSignalSocket<bool> irq_eventq;   // event-queue interrupt line
    InitiatorSignalSocket<bool> irq_priq;     // page-request-interface interrupt line
    InitiatorSignalSocket<bool> irq_cmd_sync; // pulse fired when a CMD_SYNC's IRQ-completion-signal triggers
    InitiatorSignalSocket<bool> irq_gerror;   // GERROR (Global Error) interrupt line
    InitiatorSignalSocket<bool> irq_s_gerror; // Secure-world GERROR line
    InitiatorSignalSocket<bool> irq_s_eventq; // Secure-world event-queue line
    TargetSignalSocket<bool> reset;           // hardware reset input

    std::vector<smmuv3_tbu<BUSWIDTH>*> tbus; // every TBU registers itself here at construction time

    smmuv3(sc_core::sc_module_name name);

private:
    // STE = Stream Table Entry: the in-memory record that describes one upstream device.
    // Decoded into this struct after fetching/parsing it from the stream table.
    struct STE {
        bool valid = false;        // entry has its V (Valid) bit set
        uint32_t config = 0;       // CONFIG: ABORT/BYPASS/S1/S2/NESTED (see STE_CONFIG_*)
        uint64_t s1contextptr = 0; // S1ContextPtr = pointer to the Context Descriptor table for this device
        uint64_t s2ttb = 0;        // S2TTB = Stage-2 Translation Table Base (root of the hypervisor page tables)
        uint32_t s2t0sz = 0;       // S2T0SZ = Stage-2 TTBR0 input SiZe (number of high bits ignored on input)
        uint32_t s2sl0 = 0;        // S2SL0 = Stage-2 Start Level for TTBR0 (which level the walk begins at)
        uint32_t s2tg = 0;         // S2TG = Stage-2 Translation Granule (page size of the S2 tables)
        uint32_t s2ps = 0;         // S2PS = Stage-2 Physical-address Size advertised by this STE
        uint32_t s1fmt = 0;        // S1FMT = Stage-1 stream-table format (linear vs 2-level)
        bool s1stalld = false; // S1STALLD = Stage-1 STALL Disabled (faults are reported as terminating, not stallable)
        uint32_t s1cdmax = 0;  // S1CDMAX = log2 of max number of CDs reachable through this STE
        uint16_t vmid = 0;     // VMID = Virtual Machine ID this device belongs to
        bool s2affd = false;   // S2AFFD = Stage-2 Access-Flag Fault Disable (don't fault on AF=0, ignore the flag)
        bool s2hd = false;     // S2HD = Stage-2 Hardware Dirty management enabled
        bool eats = false;     // EATS = Enable ATS for this device (PCIe Address Translation Service)
        uint8_t strw = 0;      // STRW = STReam World (STE.W1[30:31]): which CPU translation regime this device's
                               // transactions belong to (NS-EL1 / NS-EL2 / EL3). Combined with CR2.E2H and the
                               // Secure tag to disambiguate IOTLB entries that would otherwise alias.
    };

    // CD = Context Descriptor: per-(StreamID, SubstreamID) record holding the Stage-1 page-table roots.
    struct CD {
        bool valid = false;
        bool aarch64 = false;          // selects AArch64 vs AArch32 long-format page tables
        std::array<uint64_t, 2> ttb{}; // TTB[0/1] = Translation Table Base for low-half / high-half VAs (TTBR0/TTBR1)
        std::array<uint32_t, 2> tsz{}; // TSZ[0/1] = Translation SiZe: number of high VA bits ignored for each half
        std::array<uint32_t, 2> tg{};  // TG[0/1]  = Translation Granule for each half (page size)
        uint32_t ips = 0;              // IPS = Intermediate Physical-address Size (output width of stage 1)
        uint16_t asid = 0;             // ASID = Address Space ID (tags TLB entries belonging to this address space)
        std::array<bool, 2> epd{};     // EPD[0/1] = page-table walk Disable for each half (1 = unmapped half)
        bool affd = false;             // AFFD = Access-Flag Fault Disable (stage 1)
        bool ha = false;               // HA = Hardware-managed Access-flag updates allowed
        bool hd = false;               // HD = Hardware-managed Dirty-flag updates allowed
    };

    // PtwCtx = Page-Table Walk Context: input state bundled up and passed into the walker.
    struct PtwCtx {
        std::array<uint64_t, 2> ttb{}; // see CD::ttb
        std::array<uint32_t, 2> tsz{};
        std::array<uint32_t, 2> tg{};
        uint32_t ips = 0;
        uint32_t sl0 = 0; // SL0 = Start Level 0 (which page-table level to begin walking at; stage-2 only)
        std::array<bool, 2> epd{};
        int stage = 0;     // 1 = walk stage-1 tables, 2 = walk stage-2 tables
        uint64_t iova = 0; // input address for this walk (an IOVA for stage-1, an IPA for stage-2)
        IOMMUAccessFlags access = IOMMUAccessFlags::NONE; // intended access (used for permission checks)
        bool s2_enabled = false; // stage-1 walk may need each table fetch to be S2-translated first (nested)
        STE* ste = nullptr;      // pointer back to the originating STE (needed when nested)
        bool affd = false;
        bool ha = false;
        bool hd = false;
    };

    // TransReq = TRANSlation REQuest: scratch state carried through the translation pipeline. Holds inputs
    // (iova, access, sid, ssid, ste, cd) and is filled in as the walk progresses (pa, prot, fault info, ...).
    struct TransReq {
        uint64_t iova = 0;                                // input I/O Virtual Address
        IOMMUAccessFlags access = IOMMUAccessFlags::NONE; // requested access (read/write)
        uint32_t sid = 0;                                 // SID = StreamID (which device originated the transaction)
        uint32_t substream_id = 0;                        // sub-context within that device
        STE ste{};                                        // resolved Stream Table Entry
        CD cd{};                                          // resolved Context Descriptor (for stage-1)
        uint64_t pa = 0;                                  // resulting Physical Address
        IOMMUAccessFlags prot = IOMMUAccessFlags::NONE;   // permissions granted by the walk
        uint64_t page_size = 0;                           // log2 of resulting page size
        bool err = false;                                 // walk produced a fault
        uint32_t event_type = 0;                          // fault type to report (one of SMMUV3_EVT_*)
        uint32_t tableattrs = 0;     // accumulated TABLE-level attributes from intermediate descriptors
        int fault_level = 0;         // page-table level at which the fault occurred
        uint64_t descriptor = 0;     // raw leaf descriptor word
        uint64_t leaf_desc_addr = 0; // memory address of the leaf descriptor (used for HW AF/DBM updates)
    };

    // IOTLBKey = lookup key for the I/O TLB cache. Identifies one cached translation by
    // (vmid, asid, iova-page, granule, level, secure, regime). The "regime" byte folds in the
    // CPU translation regime (StreamWorld + CR2.E2H) so that two STEs with the same VMID/ASID/IOVA
    // but different regimes (e.g. NS-EL1 vs NS-EL2-E2H) do not alias.
    struct IOTLBKey {
        uint16_t vmid;  // VMID = Virtual Machine ID
        uint16_t asid;  // ASID = Address Space ID
        uint64_t iova;  // IOVA, masked to the page boundary
        uint8_t tg;     // TG = Translation Granule (page size of the cached entry)
        uint8_t level;  // page-table level at which the leaf was found
        bool secure;    // true = Secure-world entry, isolated from Non-Secure lookups
        uint8_t regime; // CPU translation regime (see compute_iotlb_regime()): EL1 vs EL2-classic vs EL2-E2H

        bool operator==(const IOTLBKey& o) const
        {
            return vmid == o.vmid && asid == o.asid && iova == o.iova && tg == o.tg && level == o.level &&
                   secure == o.secure && regime == o.regime;
        }
    };

    struct IOTLBKeyHash {
        static constexpr uint32_t VMID_HASH_SHIFT = 48;
        static constexpr uint32_t ASID_HASH_SHIFT = 32;

        static constexpr uint64_t SECURE_HASH_SALT = 0x9E3779B97F4A7C15ULL;
        static constexpr uint64_t REGIME_HASH_MULT = 0xBF58476D1CE4E5B9ULL; // mixes the regime byte into the hash
        std::size_t operator()(const IOTLBKey& k) const
        {
            uint64_t base = ((uint64_t)k.vmid << VMID_HASH_SHIFT) | ((uint64_t)k.asid << ASID_HASH_SHIFT) | k.iova;
            if (k.secure) base ^= SECURE_HASH_SALT;
            base ^= static_cast<uint64_t>(k.regime) * REGIME_HASH_MULT;
            return std::hash<uint64_t>()(base);
        }
    };

    std::unordered_map<IOTLBKey, IOMMUTLBEntry, IOTLBKeyHash> m_iotlb;
    std::list<IOTLBKey> m_iotlb_lru; // LRU = Least Recently Used: ordering used to pick a victim when the IOTLB is full

    // StalledTxn = a TLM transaction (Txn) parked here while a stallable fault is outstanding;
    // it stays here until the driver issues a RESUME command quoting our stag (Stall Tag).
    struct StalledTxn {
        tlm::tlm_generic_payload txn; // the original transaction object we're holding
        sc_core::sc_time delay;
        uint32_t stag; // STAG = Stall TAG that uniquely identifies this stalled txn
        uint32_t sid;  // originating StreamID
        uint32_t substream_id;
        sc_core::sc_event resume_event; // signalled when the driver responds (resume or abort)
        bool aborted;                   // set by the driver via the resume command if the txn must be killed
    };
    std::unordered_map<uint32_t, StalledTxn> m_stalled_txns;
    uint32_t m_next_stag; // monotonic stag generator

    // Local caches for previously-decoded STEs/CDs and previously-granted DMI regions.
    // DMI = Direct Memory Interface (TLM's fast-path mechanism for grabbing a raw pointer into a target).
    static constexpr size_t STE_CACHE_MAX = 1024;
    static constexpr size_t CD_CACHE_MAX = 1024;
    static constexpr size_t DMA_DMI_CACHE_MAX = 64;

    std::unordered_map<uint32_t, STE> m_ste_cache;
    std::list<uint32_t> m_ste_cache_lru;
    std::unordered_map<uint64_t, CD> m_cd_cache;
    std::list<uint64_t> m_cd_cache_lru;

    std::unordered_map<uint32_t, STE> m_ste_cache_secure;
    std::unordered_map<uint64_t, CD> m_cd_cache_secure;

    bool m_prev_gerror_pending = false;
    bool m_prev_eventq_pending = false;
    bool m_prev_priq_pending = false;
    bool m_prev_s_gerror_pending = false;
    bool m_prev_s_eventq_pending = false;
    bool m_last_irq_gerror_level = false;
    bool m_last_irq_eventq_level = false;
    bool m_last_irq_priq_level = false;
    bool m_last_irq_s_gerror_level = false;
    bool m_last_irq_s_eventq_level = false;

    static uint32_t extract32(uint32_t val, int start, int length) { return (val >> start) & ((1u << length) - 1); }

    static uint64_t extract64(uint64_t val, int start, int length) { return (val >> start) & ((1ULL << length) - 1); }

    // compute_iotlb_regime: collapse (STE.STRW, CR2.E2H, secure) into one byte used to tag IOTLB entries.
    // The four reachable regimes get distinct codes so cached translations cannot alias across them:
    //   IOTLB_REGIME_NS_EL1    = NS-EL1 traffic (typical guest/host stage-1)
    //   IOTLB_REGIME_NS_EL2    = NS-EL2 classic hypervisor traffic (CR2.E2H=0)
    //   IOTLB_REGIME_NS_EL2_E2H= NS-EL2-Host (VHE) traffic (CR2.E2H=1)
    //   IOTLB_REGIME_SECURE    = any Secure-world traffic; the "secure" flag in the key still applies
    static constexpr uint8_t IOTLB_REGIME_NS_EL1 = 0;
    static constexpr uint8_t IOTLB_REGIME_NS_EL2 = 1;
    static constexpr uint8_t IOTLB_REGIME_NS_EL2_E2H = 2;
    static constexpr uint8_t IOTLB_REGIME_SECURE = 3;
    static uint8_t compute_iotlb_regime(uint8_t strw, bool e2h, bool is_secure)
    {
        if (is_secure) return IOTLB_REGIME_SECURE;
        // STRW raw encoding: 0 = NS-EL1, 2 = NS-EL2; other values are reserved/secure-only in the spec.
        if ((strw & 0x3) == 0x2) return e2h ? IOTLB_REGIME_NS_EL2_E2H : IOTLB_REGIME_NS_EL2;
        return IOTLB_REGIME_NS_EL1;
    }

    bool check_s2_startlevel(unsigned int pamax_val, int level, int inputsize, int stride)
    {
        if (level < 0) return false;
        switch (stride) {
        case STRIDE_64K:
            if (level == 0 || (level == 1 && pamax_val <= 42)) return false;
            break;
        case STRIDE_16K:
            if (level == 0 || (level == 1 && pamax_val <= 40)) return false;
            break;
        case STRIDE_4K:
            if (level == 0 && pamax_val <= 42) return false;
            break;
        default:
            return false;
        }
        return true;
    }

    bool check_out_addr(uint64_t addr, unsigned int outputsize)
    {
        if (outputsize >= PA_BITS) return true;
        return extract64(addr, outputsize, PA_BITS - outputsize) == 0;
    }

    // One cached DMI (Direct Memory Interface) grant: the SystemC target told us we may read/write
    // [start..end] directly via "ptr" instead of issuing a TLM transaction every time. Used to
    // accelerate the SMMU's own page-table walks and queue accesses.
    struct DmaDmiRegion {
        sc_dt::uint64 start;
        sc_dt::uint64 end;
        unsigned char* ptr;
        bool read_allowed;
        bool write_allowed;
    };
    std::vector<DmaDmiRegion> m_dma_dmi_cache;

    const DmaDmiRegion* find_dma_dmi(uint64_t addr, size_t len, bool write) const
    {
        if (len == 0) return nullptr;
        sc_dt::uint64 last = addr + len - 1;
        for (const auto& r : m_dma_dmi_cache) {
            if (addr >= r.start && last <= r.end) {
                if ((write && r.write_allowed) || (!write && r.read_allowed)) {
                    return &r;
                }
            }
        }
        return nullptr;
    }

    bool grant_dma_dmi(uint64_t addr, tlm::tlm_command cmd)
    {
        tlm::tlm_generic_payload probe;
        probe.set_command(cmd);
        probe.set_address(addr);
        uint8_t scratch = 0;
        probe.set_data_ptr(&scratch);
        probe.set_data_length(1);
        probe.set_streaming_width(1);
        probe.set_byte_enable_length(0);
        probe.set_dmi_allowed(true);
        probe.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        tlm::tlm_dmi dmi;
        if (!dma_socket->get_direct_mem_ptr(probe, dmi)) {
            return false;
        }
        if (!dmi.is_read_allowed() && !dmi.is_write_allowed()) {
            return false;
        }
        DmaDmiRegion r;
        r.start = dmi.get_start_address();
        r.end = dmi.get_end_address();
        r.ptr = dmi.get_dmi_ptr();
        r.read_allowed = dmi.is_read_allowed();
        r.write_allowed = dmi.is_write_allowed();

        if (m_dma_dmi_cache.size() >= DMA_DMI_CACHE_MAX) {
            m_dma_dmi_cache.erase(m_dma_dmi_cache.begin());
        }
        m_dma_dmi_cache.push_back(r);
        return true;
    }

    void dma_dmi_invalidate(sc_dt::uint64 start, sc_dt::uint64 end)
    {
        m_dma_dmi_cache.erase(
            std::remove_if(m_dma_dmi_cache.begin(), m_dma_dmi_cache.end(),
                           [start, end](const DmaDmiRegion& r) { return !(r.end < start || r.start > end); }),
            m_dma_dmi_cache.end());
    }

    bool dma_read(uint64_t addr, void* data, size_t len)
    {
        const DmaDmiRegion* r = find_dma_dmi(addr, len, false);
        if (!r && grant_dma_dmi(addr, tlm::TLM_READ_COMMAND)) {
            r = find_dma_dmi(addr, len, false);
        }
        if (r) {
            std::memcpy(data, r->ptr + (addr - r->start), len);
            return true;
        }
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_address(addr);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(data));
        txn.set_data_length(len);
        txn.set_streaming_width(len);
        txn.set_byte_enable_length(0);
        txn.set_dmi_allowed(false);
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        dma_socket->b_transport(txn, delay);
        return txn.get_response_status() == tlm::TLM_OK_RESPONSE;
    }

    bool dma_write(uint64_t addr, const void* data, size_t len)
    {
        const DmaDmiRegion* r = find_dma_dmi(addr, len, true);
        if (!r && grant_dma_dmi(addr, tlm::TLM_WRITE_COMMAND)) {
            r = find_dma_dmi(addr, len, true);
        }
        if (r) {
            std::memcpy(r->ptr + (addr - r->start), data, len);
            return true;
        }
        tlm::tlm_generic_payload txn;
        txn.set_command(tlm::TLM_WRITE_COMMAND);
        txn.set_address(addr);
        txn.set_data_ptr(const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(data)));
        txn.set_data_length(len);
        txn.set_streaming_width(len);
        txn.set_byte_enable_length(0);
        txn.set_dmi_allowed(false);
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        dma_socket->b_transport(txn, delay);
        return txn.get_response_status() == tlm::TLM_OK_RESPONSE;
    }

    bool find_ste(uint32_t sid, STE& ste); // walks the stream table and decodes the STE for a StreamID
    bool read_cd(uint32_t sid, const STE& ste, uint32_t substream_id,
                 CD& cd);                          // fetches and decodes the Context Descriptor
    bool smmuv3_ptw64(PtwCtx& ctx, TransReq& req); // PTW64 = 64-bit Page-Table Walker (the heart of translation)
    IOMMUTLBEntry smmuv3_translate(tlm::tlm_generic_payload& txn, uint32_t sid, uint32_t substream_id,
                                   bool is_secure = false,
                                   bool is_ats_tr = false); // is_ats_tr = is this an ATS Translation request

    // IOTLB management: insert/lookup, plus the invalidation helpers used by TLBI commands.
    void iotlb_insert(const IOTLBKey& key, const IOMMUTLBEntry& entry);
    bool iotlb_lookup(const IOTLBKey& key, IOMMUTLBEntry& entry);
    void iotlb_inv_all();               // INV = INValidate
    void iotlb_inv_asid(uint16_t asid); // drop entries matching one ASID
    void iotlb_inv_vmid(uint16_t vmid); // drop entries matching one VMID
    void iotlb_inv_iova(uint16_t vmid, uint16_t asid, uint64_t iova, uint8_t tg,
                        uint64_t addr_mask); // drop one IOVA range

    void consume_cmdq();        // drain pending entries from the (non-secure) command queue
    void consume_secure_cmdq(); // same for the Secure-world command queue
    void record_event(uint32_t type, uint32_t sid, uint64_t iova,
                      uint32_t info); // append a simple entry to the event queue
    void record_event_full(uint32_t type, uint32_t sid, uint32_t ssid, uint64_t iova, tlm::tlm_generic_payload& txn,
                           uint32_t class_, uint32_t fault_level, uint32_t stag, bool stall); // ssid = SubstreamID
    void record_secure_event(uint32_t type, uint32_t sid, uint64_t iova, uint32_t info);
    void record_pri(uint32_t sid, uint64_t iova, uint32_t flags); // PRI = Page Request Interface entry
    bool queue_full(uint32_t prod, uint32_t cons, uint32_t size); // PROD/CONS = producer/consumer pointers

    // Per-opcode command-queue handlers; naming mirrors the CMD_OP_* opcodes above.
    void handle_cmd_prefetch_config(const uint8_t* cmd);
    void handle_cmd_prefetch_addr(const uint8_t* cmd);
    void handle_cmd_cfgi_ste(const uint8_t* cmd); // CFGI = ConFiGuration Invalidate; STE = Stream Table Entry
    void handle_cmd_cfgi_ste_range(const uint8_t* cmd);
    void handle_cmd_cfgi_cd(const uint8_t* cmd); // CD = Context Descriptor
    void handle_cmd_cfgi_cd_all(const uint8_t* cmd);
    void handle_cmd_cfgi_all(const uint8_t* cmd);
    void handle_cmd_tlbi_nh_all(const uint8_t* cmd); // TLBI = TLB Invalidate; NH = Non-Hyp scope
    void handle_cmd_tlbi_nh_asid(const uint8_t* cmd);
    void handle_cmd_tlbi_nh_va(const uint8_t* cmd);     // VA  = by Virtual Address
    void handle_cmd_tlbi_nh_vaa(const uint8_t* cmd);    // VAA = by VA, All ASIDs
    void handle_cmd_atc_inv(const uint8_t* cmd);        // ATC_INV = Address-Translation-Cache Invalidate (ATS clients)
    void handle_cmd_tlbi_s12_vmall(const uint8_t* cmd); // S12 = stages 1+2; VMALL = all entries for one VMID
    void handle_cmd_tlbi_s2_ipa(const uint8_t* cmd);    // S2 by IPA = Intermediate Physical Address
    void handle_cmd_tlbi_nsnh_all(const uint8_t* cmd);  // NSNH = Non-Secure Non-Hyp
    void handle_cmd_sync(const uint8_t* cmd);           // SYNC = command-queue barrier
    void handle_cmd_resume(const uint8_t* cmd);         // release a stalled transaction quoted by its stag
    void handle_cmd_pri_resp(const uint8_t* cmd);       // PRI_RESP = driver's response to a queued page request

    // stall_and_wait: park a translation-faulted transaction and block until the driver answers (RESUME/abort).
    // class_ = SMMUV3_CLASS_* (which subsystem caused the fault), reported back in the event record.
    bool stall_and_wait(uint32_t sid, uint32_t ssid, uint64_t iova, uint32_t fault_type, uint32_t fault_level,
                        uint32_t class_, tlm::tlm_generic_payload& txn);

    void do_gatos(); // run one Global Address Translation Operation (debug translate-this-VA channel; see GATOS_*)

    // Pulse a wired interrupt line: high then low (edge-triggered receivers see one event).
    void trigger_irq(InitiatorSignalSocket<bool>& irq)
    {
        if (irq.size() > 0) {
            irq->write(true);
            irq->write(false);
        }
    }

    // Set one bit in GERROR (Global Error). The driver later acknowledges by writing the same bit
    // into GERRORN; until then the bit position remains "live" and edge-triggers no further IRQs.
    void set_gerror(uint32_t bit)
    {
        uint32_t err = static_cast<uint32_t>(GERROR);
        uint32_t ack = static_cast<uint32_t>(GERRORN);
        if (((err ^ ack) >> bit) & 1u) {
            return;
        }
        GERROR = err ^ (1u << bit);
        update_irq_levels();
    }

    void set_secure_gerror(uint32_t bit)
    {
        uint32_t err = static_cast<uint32_t>(S_GERROR);
        uint32_t ack = static_cast<uint32_t>(S_GERRORN);
        if (((err ^ ack) >> bit) & 1u) {
            return;
        }
        S_GERROR = err ^ (1u << bit);
        update_secure_irq_levels();
    }

    void set_cmdq_err(uint8_t cerror)
    {
        CMDQ_CONS[CMDQ_CONS_ERR] = cerror;
        set_gerror(GERROR_BIT_CMDQ_ERR);
    }

    void set_secure_cmdq_err(uint8_t cerror)
    {
        S_CMDQ_CONS[S_CMDQ_CONS_ERR] = cerror;
        set_secure_gerror(GERROR_BIT_CMDQ_ERR);
    }

    void drive_irq_level(InitiatorSignalSocket<bool>& irq, bool& last_level, bool new_level)
    {
        if (last_level == new_level) return;
        last_level = new_level;
        if (irq.size() > 0) {
            irq->write(new_level);
        }
    }

    // Send one Message-Signalled Interrupt: a 32-bit "data" word is DMA-written to the address built
    // from cfg0_hi:cfg0_lo. If the write fails, set the matching abort bit in GERROR.
    void emit_msi(uint32_t cfg0_lo, uint32_t cfg0_hi, uint32_t data, uint32_t abort_bit_on_fail)
    {
        uint64_t addr = (static_cast<uint64_t>(cfg0_hi) << 32) | static_cast<uint64_t>(cfg0_lo);
        if (addr == 0) return;
        uint32_t payload = data;
        if (!dma_write(addr, &payload, sizeof(payload))) {
            set_gerror(abort_bit_on_fail);
        }
    }

    void update_irq_levels()
    {
        bool gerror_pending = (static_cast<uint32_t>(GERROR) ^ static_cast<uint32_t>(GERRORN)) != 0;
        bool eventq_pending = (static_cast<uint32_t>(EVENTQ_PROD) != static_cast<uint32_t>(EVENTQ_CONS));
        bool priq_pending = (static_cast<uint32_t>(PRIQ_PROD) != static_cast<uint32_t>(PRIQ_CONS));

        bool gerror_irq = gerror_pending && static_cast<uint32_t>(IRQ_CTRL[IRQ_CTRL_GERROR_IRQEN]);
        bool eventq_irq = eventq_pending && static_cast<uint32_t>(IRQ_CTRL[IRQ_CTRL_EVENTQ_IRQEN]);
        bool priq_irq = priq_pending && static_cast<uint32_t>(IRQ_CTRL[IRQ_CTRL_PRI_IRQEN]);

        if (gerror_pending && !m_prev_gerror_pending && static_cast<uint32_t>(IRQ_CTRL[IRQ_CTRL_GERROR_IRQEN])) {
            emit_msi(static_cast<uint32_t>(GERROR_IRQ_CFG0_LO), static_cast<uint32_t>(GERROR_IRQ_CFG0_HI),
                     static_cast<uint32_t>(GERROR_IRQ_CFG2), GERROR_BIT_MSI_GERROR_ABT);
        }
        if (eventq_pending && !m_prev_eventq_pending && static_cast<uint32_t>(IRQ_CTRL[IRQ_CTRL_EVENTQ_IRQEN])) {
            emit_msi(static_cast<uint32_t>(EVENTQ_IRQ_CFG0_LO), static_cast<uint32_t>(EVENTQ_IRQ_CFG0_HI),
                     static_cast<uint32_t>(EVENTQ_IRQ_CFG2), GERROR_BIT_MSI_EVENTQ_ABT);
        }
        if (priq_pending && !m_prev_priq_pending && static_cast<uint32_t>(IRQ_CTRL[IRQ_CTRL_PRI_IRQEN])) {
            emit_msi(static_cast<uint32_t>(PRIQ_IRQ_CFG0_LO), static_cast<uint32_t>(PRIQ_IRQ_CFG0_HI),
                     static_cast<uint32_t>(PRIQ_IRQ_CFG2), GERROR_BIT_MSI_PRIQ_ABT);
        }

        m_prev_gerror_pending = gerror_pending;
        m_prev_eventq_pending = eventq_pending;
        m_prev_priq_pending = priq_pending;

        drive_irq_level(irq_gerror, m_last_irq_gerror_level, gerror_irq);
        drive_irq_level(irq_eventq, m_last_irq_eventq_level, eventq_irq);
        drive_irq_level(irq_priq, m_last_irq_priq_level, priq_irq);
    }

    void update_secure_irq_levels()
    {
        bool gerror_pending = (static_cast<uint32_t>(S_GERROR) ^ static_cast<uint32_t>(S_GERRORN)) != 0;
        bool eventq_pending = (static_cast<uint32_t>(S_EVENTQ_PROD) != static_cast<uint32_t>(S_EVENTQ_CONS));

        bool gerror_irq = gerror_pending && static_cast<uint32_t>(S_IRQ_CTRL[S_IRQ_CTRL_GERROR_IRQEN]);
        bool eventq_irq = eventq_pending && static_cast<uint32_t>(S_IRQ_CTRL[S_IRQ_CTRL_EVENTQ_IRQEN]);

        if (gerror_pending && !m_prev_s_gerror_pending && static_cast<uint32_t>(S_IRQ_CTRL[S_IRQ_CTRL_GERROR_IRQEN])) {
            emit_msi(static_cast<uint32_t>(S_GERROR_IRQ_CFG0_LO), static_cast<uint32_t>(S_GERROR_IRQ_CFG0_HI),
                     static_cast<uint32_t>(S_GERROR_IRQ_CFG2), GERROR_BIT_MSI_GERROR_ABT);
        }
        if (eventq_pending && !m_prev_s_eventq_pending && static_cast<uint32_t>(S_IRQ_CTRL[S_IRQ_CTRL_EVENTQ_IRQEN])) {
            emit_msi(static_cast<uint32_t>(S_EVENTQ_IRQ_CFG0_LO), static_cast<uint32_t>(S_EVENTQ_IRQ_CFG0_HI),
                     static_cast<uint32_t>(S_EVENTQ_IRQ_CFG2), GERROR_BIT_MSI_EVENTQ_ABT);
        }

        m_prev_s_gerror_pending = gerror_pending;
        m_prev_s_eventq_pending = eventq_pending;

        drive_irq_level(irq_s_gerror, m_last_irq_s_gerror_level, gerror_irq);
        drive_irq_level(irq_s_eventq, m_last_irq_s_eventq_level, eventq_irq);
    }

public:
    friend class smmuv3_tbu<BUSWIDTH>;

    void start_of_simulation() override;
    void before_end_of_elaboration() override;

    void test_inject_pri(uint32_t sid, uint64_t iova, uint32_t flags) { record_pri(sid, iova, flags); }
    void test_inject_secure_event(uint32_t type, uint32_t sid, uint64_t iova, uint32_t info)
    {
        record_secure_event(type, sid, iova, info);
    }
    void test_inject_stall(uint32_t stag)
    {
        auto& s = m_stalled_txns[stag];
        s.stag = stag;
        s.aborted = false;
    }
    bool test_stall_present(uint32_t stag) const { return m_stalled_txns.count(stag) != 0; }
    bool test_stall_aborted(uint32_t stag) const
    {
        auto it = m_stalled_txns.find(stag);
        return it != m_stalled_txns.end() && it->second.aborted;
    }
    size_t test_iotlb_size() const { return m_iotlb.size(); }
    size_t test_ste_cache_size() const { return m_ste_cache.size(); }
    size_t test_cd_cache_size() const { return m_cd_cache.size(); }
};

// TBU = Translation Buffer Unit: per-master front-end. One TBU sits between an upstream device's
// transaction port and the SMMU core; it forwards each transaction through smmuv3_translate() and
// then on to the downstream interconnect with the translated address.
template <unsigned int BUSWIDTH>
class smmuv3_tbu : public sc_core::sc_module
{
    SCP_LOGGER();
    smmuv3<BUSWIDTH>* m_smmu;

    // Snapshot describing one IOVA-page projection of the downstream memory map (used when serving DMI).
    struct MemoryView {
        uint64_t address;
        uint64_t page_start;
        uint64_t page_end;
        uint64_t page_size;
    };

    static uint32_t extract_substream_id(tlm::tlm_generic_payload& txn)
    {
        auto* ssx = txn.get_extension<smmuv3_ss_extension>();
        return (ssx && ssx->ssv) ? ssx->substream_id : 0;
    }

    static bool extract_secure(tlm::tlm_generic_payload& txn)
    {
        auto* sx = txn.get_extension<smmuv3_secure_extension>();
        return sx && sx->secure;
    }

public:
    void upstream_invalidate(sc_dt::uint64 start, sc_dt::uint64 end)
    {
        if (upstream_socket.size() == 0) return;
        upstream_socket->invalidate_direct_mem_ptr(start, end);
    }

protected:
    void b_transport(tlm::tlm_generic_payload& txn, sc_core::sc_time& delay)
    {
        sc_dt::uint64 addr = txn.get_address();
        tlm::tlm_command cmd = txn.get_command();

        auto* ats = txn.get_extension<smmuv3_ats_extension>();
        uint32_t substream_id = extract_substream_id(txn);
        bool is_secure = extract_secure(txn);

        if (ats && ats->is_translated) {
            downstream_socket->b_transport(txn, delay);
            return;
        }

        typename smmuv3<BUSWIDTH>::IOMMUTLBEntry te = m_smmu->smmuv3_translate(
            txn, p_topology_id, substream_id, is_secure, ats && ats->is_translation_request);

        while (te.stallable_fault) {
            bool aborted = m_smmu->stall_and_wait(p_topology_id, substream_id, addr, te.fault_type, te.fault_level, 0,
                                                  txn);
            if (aborted) {
                txn.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
            }
            te = m_smmu->smmuv3_translate(txn, p_topology_id, substream_id, is_secure,
                                          ats && ats->is_translation_request);
        }

        if (ats && ats->is_translation_request) {
            if (te.perm == IOMMUAccessFlags::NONE) {
                ats->faulted = true;
                ats->fault_type = te.fault_type;
                ats->pa = 0;
                ats->granule_log2 = 0;
                ats->prot = 0;
                if (static_cast<uint32_t>(m_smmu->CR0[m_smmu->CR0_PRIQEN]) &&
                    static_cast<uint32_t>(m_smmu->IDR0[m_smmu->IDR0_PRI])) {
                    uint32_t flags = (cmd == tlm::TLM_WRITE_COMMAND ? 0x2u : 0x0u) | (ats->prg_index << 16);
                    m_smmu->record_pri(p_topology_id, addr, flags);
                }
            } else {
                ats->faulted = false;
                ats->pa = te.translated_addr | (addr & te.addr_mask);
                uint64_t m = te.addr_mask + 1;
                uint8_t g = 0;
                while (m > 1) {
                    m >>= 1;
                    ++g;
                }
                ats->granule_log2 = g;
                ats->prot = static_cast<uint8_t>(te.perm);
            }
            txn.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }

        if (te.perm == IOMMUAccessFlags::NONE || (cmd == tlm::TLM_WRITE_COMMAND && te.perm == IOMMUAccessFlags::RO) ||
            (cmd == tlm::TLM_READ_COMMAND && te.perm == IOMMUAccessFlags::WO)) {
            txn.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        } else {
            txn.set_address(te.translated_addr | (addr & SMMUV3_PAGEMASK));

            auto ext = std::make_unique<smmuv3_memory_attrs_extension>();
            ext->set_from_descriptor(te.descriptor, te.table_attrs);
            txn.set_extension(ext.get());

            SCP_INFO(()) << std::hex << "smmuv3 TBU b_transport: translate 0x" << addr << " to 0x"
                         << (te.translated_addr | (addr & SMMUV3_PAGEMASK)) << " attrs=" << ext->to_string();

            downstream_socket->b_transport(txn, delay);
            txn.set_address(addr);
            txn.clear_extension<smmuv3_memory_attrs_extension>();
        }
    }

    virtual unsigned int transport_dbg(tlm::tlm_generic_payload& txn)
    {
        sc_dt::uint64 addr = txn.get_address();
        uint32_t substream_id = extract_substream_id(txn);
        bool is_secure = extract_secure(txn);
        typename smmuv3<BUSWIDTH>::IOMMUTLBEntry te = m_smmu->smmuv3_translate(txn, p_topology_id, substream_id,
                                                                               is_secure, false);
        txn.set_address(te.translated_addr | (addr & SMMUV3_PAGEMASK));
        int ret = downstream_socket->transport_dbg(txn);
        txn.set_address(addr);
        return ret;
    }

    virtual bool get_direct_mem_ptr(tlm::tlm_generic_payload& txn, tlm::tlm_dmi& dmi_data)
    {
        sc_dt::uint64 iova = txn.get_address();
        uint32_t substream_id = extract_substream_id(txn);
        bool is_secure = extract_secure(txn);
        typename smmuv3<BUSWIDTH>::IOMMUTLBEntry te = m_smmu->smmuv3_translate(txn, p_topology_id, substream_id,
                                                                               is_secure, false);

        if (te.perm == IOMMUAccessFlags::NONE) {
            txn.set_address(iova);
            dmi_data.allow_none();
            dmi_data.set_start_address(iova);
            dmi_data.set_end_address(iova);
            return false;
        }

        tlm::tlm_command cmd = txn.get_command();
        if ((cmd == tlm::TLM_WRITE_COMMAND && te.perm == IOMMUAccessFlags::RO) ||
            (cmd == tlm::TLM_READ_COMMAND && te.perm == IOMMUAccessFlags::WO)) {
            txn.set_address(iova);
            dmi_data.allow_none();
            dmi_data.set_start_address(iova);
            dmi_data.set_end_address(iova);
            return false;
        }

        sc_dt::uint64 offset_in_page = iova & te.addr_mask;
        sc_dt::uint64 pa = te.translated_addr | offset_in_page;
        txn.set_address(pa);
        bool ok = downstream_socket->get_direct_mem_ptr(txn, dmi_data);
        txn.set_address(iova);
        if (!ok) return false;

        sc_dt::uint64 pa_page_start = te.translated_addr;
        sc_dt::uint64 pa_page_end = te.translated_addr | te.addr_mask;
        sc_dt::uint64 dmi_start = dmi_data.get_start_address();
        sc_dt::uint64 dmi_end = dmi_data.get_end_address();

        sc_dt::uint64 clamp_pa_start = pa_page_start > dmi_start ? pa_page_start : dmi_start;
        sc_dt::uint64 clamp_pa_end = pa_page_end < dmi_end ? pa_page_end : dmi_end;
        if (clamp_pa_end < clamp_pa_start) return false;

        sc_dt::uint64 iova_page_start = iova & ~te.addr_mask;
        sc_dt::uint64 new_iova_start = iova_page_start + (clamp_pa_start - pa_page_start);
        sc_dt::uint64 new_iova_end = iova_page_start + (clamp_pa_end - pa_page_start);

        unsigned char* new_dmi_ptr = dmi_data.get_dmi_ptr() + (clamp_pa_start - dmi_start);

        dmi_data.set_dmi_ptr(new_dmi_ptr);
        dmi_data.set_start_address(new_iova_start);
        dmi_data.set_end_address(new_iova_end);

        if (te.perm == IOMMUAccessFlags::RO) {
            dmi_data.allow_read();
        } else if (te.perm == IOMMUAccessFlags::WO) {
            dmi_data.allow_write();
        }
        return true;
    }

    virtual void invalidate_direct_mem_ptr(sc_dt::uint64 start, sc_dt::uint64 end)
    {
        if (!m_smmu) return;
        for (const auto& e : m_smmu->m_iotlb) {
            const auto& entry = e.second;
            sc_dt::uint64 pa_start = entry.translated_addr;
            sc_dt::uint64 pa_end = entry.translated_addr | entry.addr_mask;
            if (pa_start > end || pa_end < start) continue;
            sc_dt::uint64 iova_start = entry.iova;
            sc_dt::uint64 iova_end = entry.iova | entry.addr_mask;
            upstream_socket->invalidate_direct_mem_ptr(iova_start, iova_end);
        }
    }

public:
    // p_topology_id is the StreamID this TBU stamps onto every incoming transaction (i.e. "I am device N").
    cci::cci_param<uint32_t> p_topology_id;
    tlm_utils::simple_target_socket<smmuv3_tbu> upstream_socket; // accepts transactions from the master
    tlm_utils::simple_initiator_socket<smmuv3_tbu>
        downstream_socket; // forwards translated transactions to the interconnect

    smmuv3_tbu(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : smmuv3_tbu(name, dynamic_cast<smmuv3<BUSWIDTH>*>(o))
    {
    }

    smmuv3_tbu(sc_core::sc_module_name name, smmuv3<BUSWIDTH>* smmu)
        : sc_core::sc_module(name)
        , m_smmu(smmu)
        , p_topology_id("topology_id", 0)
        , upstream_socket("upstream_socket")
        , downstream_socket("downstream_socket")
    {
        if (m_smmu) {
            m_smmu->tbus.push_back(this);
        }

        upstream_socket.register_b_transport(this, &smmuv3_tbu::b_transport);
        upstream_socket.register_transport_dbg(this, &smmuv3_tbu::transport_dbg);
        upstream_socket.register_get_direct_mem_ptr(this, &smmuv3_tbu::get_direct_mem_ptr);
        downstream_socket.register_invalidate_direct_mem_ptr(this, &smmuv3_tbu::invalidate_direct_mem_ptr);
    }
};

template <unsigned int BUSWIDTH>
smmuv3<BUSWIDTH>::smmuv3(sc_core::sc_module_name name)
    : sc_core::sc_module(name)
    , smmuv3_gen()
    , m_broker(cci::cci_get_broker())
    , m_jza(zip_open_from_source(
          zip_source_buffer_create(smmuv3_zip_archive_data(), smmuv3_zip_archive_size(), 0, nullptr), ZIP_RDONLY,
          nullptr))
    , m_loaded_ok(m_jza.json_read_cci(m_broker, std::string(this->name()) + ".smmuv3"))
    , M("smmuv3", m_jza)
    , p_pamax("pamax", 48)
    , p_sidsize("sidsize", 16)
    , p_ato("ato", true)
    , p_num_tbu("num_tbu", 1)
    , p_iotlb_size("iotlb_size", 256)
    , p_iidr("iidr", 0x00000000u)
    , socket("target_socket")
    , dma_socket("dma")
    , irq_eventq("irq_eventq")
    , irq_priq("irq_priq")
    , irq_cmd_sync("irq_cmd_sync")
    , irq_gerror("irq_gerror")
    , irq_s_gerror("irq_s_gerror")
    , irq_s_eventq("irq_s_eventq")
    , reset("reset")
    , m_next_stag(1)
{
    SCP_LOGGER();
    sc_assert(m_loaded_ok);

    socket.bind(M.target_socket);
    bind_regs(M);

    dma_socket.register_invalidate_direct_mem_ptr(this, &smmuv3<BUSWIDTH>::dma_dmi_invalidate);

    reset.register_value_changed_cb([this](bool v) {
        if (v) {
            m_iotlb.clear();
            m_iotlb_lru.clear();
            m_ste_cache.clear();
            m_ste_cache_lru.clear();
            m_cd_cache.clear();
            m_cd_cache_lru.clear();
            m_stalled_txns.clear();
            m_dma_dmi_cache.clear();
        } else {
            start_of_simulation();
        }
    });
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::start_of_simulation()
{
    IDR0 = 0;
    IDR0[IDR0_S2P] = 1;
    IDR0[IDR0_S1P] = 1;
    IDR0[IDR0_TTF] = IDR0_TTF_AARCH64;
    IDR0[IDR0_COHACC] = 1;
    IDR0[IDR0_ASID16] = 1;
    IDR0[IDR0_VMID16] = 1;
    IDR0[IDR0_PRI] = 1;
    IDR0[IDR0_ATOS] = static_cast<uint32_t>(p_ato.get_value());
    IDR0[IDR0_HTTU] = 3;
    IDR0[IDR0_STLEVEL] = IDR0_STLEVEL_2LVL;
    IDR0[IDR0_MSI] = 1;
    IDR0[IDR0_ATS] = 1;

    IDR1 = 0;
    IDR1[IDR1_SIDSIZE] = p_sidsize.get_value();
    IDR1[IDR1_EVENTQS] = IDR1_DEFAULT_QUEUE_LOG2;
    IDR1[IDR1_CMDQS] = IDR1_DEFAULT_QUEUE_LOG2;

    IDR5 = 0;
    uint32_t oas_val = OAS_48;
    switch (p_pamax.get_value()) {
    case 32:
        oas_val = OAS_32;
        break;
    case 36:
        oas_val = OAS_36;
        break;
    case 40:
        oas_val = OAS_40;
        break;
    case 42:
        oas_val = OAS_42;
        break;
    case 44:
        oas_val = OAS_44;
        break;
    case 48:
        oas_val = OAS_48;
        break;
    default:
        oas_val = OAS_48;
        break;
    }
    IDR5[IDR5_OAS] = oas_val;
    IDR5[IDR5_GRAN4K] = 1;
    IDR5[IDR5_GRAN16K] = 1;
    IDR5[IDR5_GRAN64K] = 1;

    for (size_t i = 0; i < PRIMECELL_IDS.size(); ++i) {
        *IDREGS[i] = PRIMECELL_IDS[i];
    }

    IIDR = p_iidr.get_value();

    GBPA[GBPA_ABORT] = 1;

    // Reset all writable runtime registers to zero. The reset-values .hex only seeds the read-only
    // ID/IIDR/GBPA registers, and the gs_register backing is not guaranteed to be zero-initialised
    // on every host (observed on macOS-15 debug runners: registers come back as recycled function
    // pointers, e.g. S_GERROR=0x10274150 / S_GERRORN=0x00000001, which causes set_secure_gerror's
    // "already pending" check to short-circuit and never emit the MSI doorbell).
    CR0 = 0;
    CR0ACK = 0;
    CR1 = 0;
    CR2 = 0;
    STATUSR = 0;
    AGBPA = 0;
    IRQ_CTRL = 0;
    IRQ_CTRL_ACK = 0;
    GERROR = 0;
    GERRORN = 0;
    GERROR_IRQ_CFG0_LO = 0;
    GERROR_IRQ_CFG0_HI = 0;
    GERROR_IRQ_CFG1 = 0;
    GERROR_IRQ_CFG2 = 0;
    STRTAB_BASE_LO = 0;
    STRTAB_BASE_HI = 0;
    STRTAB_BASE_CFG = 0;
    CMDQ_BASE_LO = 0;
    CMDQ_BASE_HI = 0;
    CMDQ_PROD = 0;
    CMDQ_CONS = 0;
    EVENTQ_BASE_LO = 0;
    EVENTQ_BASE_HI = 0;
    EVENTQ_PROD = 0;
    EVENTQ_CONS = 0;
    EVENTQ_IRQ_CFG0_LO = 0;
    EVENTQ_IRQ_CFG0_HI = 0;
    EVENTQ_IRQ_CFG1 = 0;
    EVENTQ_IRQ_CFG2 = 0;
    PRIQ_BASE_LO = 0;
    PRIQ_BASE_HI = 0;
    PRIQ_PROD = 0;
    PRIQ_CONS = 0;
    PRIQ_IRQ_CFG0_LO = 0;
    PRIQ_IRQ_CFG0_HI = 0;
    PRIQ_IRQ_CFG1 = 0;
    PRIQ_IRQ_CFG2 = 0;
    GATOS_CTRL = 0;
    GATOS_SID = 0;
    GATOS_ADDR_LO = 0;
    GATOS_ADDR_HI = 0;
    GATOS_PAR_LO = 0;
    GATOS_PAR_HI = 0;

    // Secure-world mirrors of the same registers.
    S_IDR0 = 0;
    S_IDR1 = 0;
    S_IDR2 = 0;
    S_IDR3 = 0;
    S_IDR4 = 0;
    S_CR0 = 0;
    S_CR0ACK = 0;
    S_CR1 = 0;
    S_CR2 = 0;
    S_INIT = 0;
    S_GBPA = 0;
    S_AGBPA = 0;
    S_IRQ_CTRL = 0;
    S_IRQ_CTRL_ACK = 0;
    S_GERROR = 0;
    S_GERRORN = 0;
    S_GERROR_IRQ_CFG0_LO = 0;
    S_GERROR_IRQ_CFG0_HI = 0;
    S_GERROR_IRQ_CFG1 = 0;
    S_GERROR_IRQ_CFG2 = 0;
    S_STRTAB_BASE_LO = 0;
    S_STRTAB_BASE_HI = 0;
    S_STRTAB_BASE_CFG = 0;
    S_CMDQ_BASE_LO = 0;
    S_CMDQ_BASE_HI = 0;
    S_CMDQ_PROD = 0;
    S_CMDQ_CONS = 0;
    S_EVENTQ_BASE_LO = 0;
    S_EVENTQ_BASE_HI = 0;
    S_EVENTQ_PROD = 0;
    S_EVENTQ_CONS = 0;
    S_EVENTQ_IRQ_CFG0_LO = 0;
    S_EVENTQ_IRQ_CFG0_HI = 0;
    S_EVENTQ_IRQ_CFG1 = 0;
    S_EVENTQ_IRQ_CFG2 = 0;

    // Page-1 aliases of the queue pointers; pre_read refreshes them later but make the initial
    // visible state deterministic too.
    CMDQ_PROD_P1 = 0;
    CMDQ_CONS_P1 = 0;
    EVENTQ_PROD_P1 = 0;
    EVENTQ_CONS_P1 = 0;
    PRIQ_PROD_P1 = 0;
    PRIQ_CONS_P1 = 0;
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::before_end_of_elaboration()
{
    SCP_INFO(()) << "SMMUv3: registering post_write callbacks";

    CR0.post_write([this](tlm::tlm_generic_payload&, sc_core::sc_time&) {
        uint32_t cr0 = static_cast<uint32_t>(CR0);
        SCP_INFO(()) << "SMMUv3: CR0 write = 0x" << std::hex << cr0;
        CR0ACK = cr0;
    });

    IRQ_CTRL.post_write([this](tlm::tlm_generic_payload&, sc_core::sc_time&) {
        uint32_t val = static_cast<uint32_t>(IRQ_CTRL);
        SCP_INFO(()) << "SMMUv3: IRQ_CTRL write = 0x" << std::hex << val;
        IRQ_CTRL_ACK = val;
        update_irq_levels();
    });

    GERRORN.post_write([this](tlm::tlm_generic_payload&, sc_core::sc_time&) { update_irq_levels(); });
    EVENTQ_CONS.post_write([this](tlm::tlm_generic_payload&, sc_core::sc_time&) { update_irq_levels(); });
    PRIQ_CONS.post_write([this](tlm::tlm_generic_payload&, sc_core::sc_time&) { update_irq_levels(); });

    S_CR0.post_write([this](tlm::tlm_generic_payload&, sc_core::sc_time&) {
        S_CR0ACK = static_cast<uint32_t>(S_CR0);
        update_secure_irq_levels();
    });
    S_IRQ_CTRL.post_write([this](tlm::tlm_generic_payload&, sc_core::sc_time&) {
        S_IRQ_CTRL_ACK = static_cast<uint32_t>(S_IRQ_CTRL);
        update_secure_irq_levels();
    });
    S_GERRORN.post_write([this](tlm::tlm_generic_payload&, sc_core::sc_time&) { update_secure_irq_levels(); });
    S_EVENTQ_CONS.post_write([this](tlm::tlm_generic_payload&, sc_core::sc_time&) { update_secure_irq_levels(); });

    S_CMDQ_PROD.post_write([this](tlm::tlm_generic_payload&, sc_core::sc_time&) {
        if (static_cast<uint32_t>(S_CR0[S_CR0_CMDQEN])) {
            consume_secure_cmdq();
        }
    });

    S_CMDQ_CONS.pre_read([this](tlm::tlm_generic_payload&, sc_core::sc_time&) {
        if (static_cast<uint32_t>(S_CR0[S_CR0_CMDQEN])) {
            consume_secure_cmdq();
        }
    });

    CMDQ_PROD.post_write([this](tlm::tlm_generic_payload&, sc_core::sc_time&) {
        uint32_t prod = static_cast<uint32_t>(CMDQ_PROD);
        SCP_INFO(()) << "SMMUv3: CMDQ_PROD write = 0x" << std::hex << prod;
        if (static_cast<uint32_t>(CR0[CR0_CMDQEN])) {
            consume_cmdq();
        }
    });

    CMDQ_CONS.pre_read([this](tlm::tlm_generic_payload&, sc_core::sc_time&) {
        if (static_cast<uint32_t>(CR0[CR0_CMDQEN])) {
            consume_cmdq();
        }
    });

    STRTAB_BASE_CFG.post_write([this](tlm::tlm_generic_payload&, sc_core::sc_time&) {
        m_ste_cache.clear();
        m_ste_cache_lru.clear();
    });

    GBPA.post_write([this](tlm::tlm_generic_payload&, sc_core::sc_time&) { GBPA[GBPA_UPDATE] = 0u; });
    S_GBPA.post_write([this](tlm::tlm_generic_payload&, sc_core::sc_time&) { S_GBPA[S_GBPA_UPDATE] = 0u; });
    S_INIT.post_write([this](tlm::tlm_generic_payload&, sc_core::sc_time&) {
        if (static_cast<uint32_t>(S_INIT[S_INIT_INV_ALL])) {
            m_iotlb.clear();
            m_iotlb_lru.clear();
            m_ste_cache.clear();
            m_ste_cache_lru.clear();
            m_cd_cache.clear();
            m_cd_cache_lru.clear();
            m_ste_cache_secure.clear();
            m_cd_cache_secure.clear();
            S_INIT[S_INIT_INV_ALL] = 0u;
        }
    });

    GATOS_CTRL.post_write([this](tlm::tlm_generic_payload&, sc_core::sc_time&) {
        if (static_cast<uint32_t>(GATOS_CTRL[GATOS_CTRL_RUN])) {
            do_gatos();
        }
    });

    PRIQ_CONS.post_write([this](tlm::tlm_generic_payload&, sc_core::sc_time&) {});

    CMDQ_PROD_P1.post_write([this](tlm::tlm_generic_payload&, sc_core::sc_time&) {
        CMDQ_PROD = static_cast<uint32_t>(CMDQ_PROD_P1);
        if (static_cast<uint32_t>(CR0[CR0_CMDQEN])) {
            consume_cmdq();
        }
    });

    EVENTQ_CONS_P1.post_write(
        [this](tlm::tlm_generic_payload&, sc_core::sc_time&) { EVENTQ_CONS = static_cast<uint32_t>(EVENTQ_CONS_P1); });

    PRIQ_CONS_P1.post_write(
        [this](tlm::tlm_generic_payload&, sc_core::sc_time&) { PRIQ_CONS = static_cast<uint32_t>(PRIQ_CONS_P1); });

    CMDQ_PROD_P1.pre_read(
        [this](tlm::tlm_generic_payload&, sc_core::sc_time&) { CMDQ_PROD_P1 = static_cast<uint32_t>(CMDQ_PROD); });
    CMDQ_CONS_P1.pre_read([this](tlm::tlm_generic_payload&, sc_core::sc_time&) {
        if (static_cast<uint32_t>(CR0[CR0_CMDQEN])) consume_cmdq();
        CMDQ_CONS_P1 = static_cast<uint32_t>(CMDQ_CONS);
    });
    EVENTQ_PROD_P1.pre_read(
        [this](tlm::tlm_generic_payload&, sc_core::sc_time&) { EVENTQ_PROD_P1 = static_cast<uint32_t>(EVENTQ_PROD); });
    EVENTQ_CONS_P1.pre_read(
        [this](tlm::tlm_generic_payload&, sc_core::sc_time&) { EVENTQ_CONS_P1 = static_cast<uint32_t>(EVENTQ_CONS); });
    PRIQ_PROD_P1.pre_read(
        [this](tlm::tlm_generic_payload&, sc_core::sc_time&) { PRIQ_PROD_P1 = static_cast<uint32_t>(PRIQ_PROD); });
    PRIQ_CONS_P1.pre_read(
        [this](tlm::tlm_generic_payload&, sc_core::sc_time&) { PRIQ_CONS_P1 = static_cast<uint32_t>(PRIQ_CONS); });
}

template <unsigned int BUSWIDTH>
bool smmuv3<BUSWIDTH>::find_ste(uint32_t sid, STE& ste)
{
    auto it = m_ste_cache.find(sid);
    if (it != m_ste_cache.end()) {
        ste = it->second;
        return ste.valid;
    }

    constexpr uint64_t L1STD_VALID_BIT = 1;
    constexpr uint32_t L1STD_PTR_LOW_BITS = 6;
    constexpr size_t L1STD_SIZE = 8;
    constexpr uint32_t STRTAB_FMT_LINEAR = 0;

    uint64_t strtab_base = ((static_cast<uint64_t>(STRTAB_BASE_HI) << 32) | static_cast<uint64_t>(STRTAB_BASE_LO)) &
                           STE_BASE_ADDR_MASK;
    uint32_t fmt = static_cast<uint32_t>(STRTAB_BASE_CFG[STRTAB_BASE_CFG_FMT]);

    uint32_t log2size = static_cast<uint32_t>(STRTAB_BASE_CFG[STRTAB_BASE_CFG_LOG2SIZE]);
    if (log2size > 31u) log2size = 31u;

    std::array<uint8_t, SMMUV3_STE_SIZE> ste_buf{};
    uint64_t ste_addr;

    if (fmt == STRTAB_FMT_LINEAR) {
        if (sid >= (1u << log2size)) {
            return false;
        }
        ste_addr = strtab_base + (sid * SMMUV3_STE_SIZE);
    } else {
        uint32_t split = static_cast<uint32_t>(STRTAB_BASE_CFG[STRTAB_BASE_CFG_SPLIT]);
        uint32_t l1_idx = sid >> split;
        uint32_t l2_idx = sid & ((1u << split) - 1);

        if (l1_idx >= (1u << log2size)) {
            return false;
        }

        uint64_t l1std_addr = strtab_base + (l1_idx * L1STD_SIZE);
        uint64_t l1std;
        if (!dma_read(l1std_addr, &l1std, L1STD_SIZE)) {
            set_gerror(GERROR_BIT_SFM_ERR);
            return false;
        }

        if ((l1std & L1STD_VALID_BIT) == 0) {
            return false;
        }

        uint64_t l2ptr = l1std & ~((1ULL << L1STD_PTR_LOW_BITS) - 1);
        ste_addr = l2ptr + (l2_idx * SMMUV3_STE_SIZE);
    }

    if (!dma_read(ste_addr, ste_buf.data(), SMMUV3_STE_SIZE)) {
        set_gerror(GERROR_BIT_SFM_ERR);
        return false;
    }

    uint64_t dw0 = load_le<uint64_t>(ste_buf.data() + 0);
    uint64_t dw1 = load_le<uint64_t>(ste_buf.data() + 8);
    uint64_t dw2 = load_le<uint64_t>(ste_buf.data() + 16);
    uint64_t dw3 = load_le<uint64_t>(ste_buf.data() + 24);

    ste.valid = dw0 & 1;
    ste.config = extract64(dw0, 1, 3);
    ste.s1fmt = extract64(dw0, 4, 2);
    ste.s1contextptr = dw0 & STE_BASE_ADDR_MASK;
    ste.s1cdmax = extract64(dw0, 59, 5);

    ste.s1stalld = extract64(dw1, 27, 1);
    ste.eats = extract64(dw1, 28, 2);
    ste.strw = extract64(dw1, 30,
                         2); // STRW: 0=NS-EL1, 2=NS-EL2 (E2H further selected by CR2.E2H), other=secure-only/reserved

    ste.vmid = extract64(dw2, 0, 16);
    ste.s2t0sz = extract64(dw2, 32, 6);
    ste.s2sl0 = extract64(dw2, 38, 2);
    ste.s2tg = extract64(dw2, 46, 2);
    ste.s2ps = extract64(dw2, 48, 3);
    ste.s2hd = extract64(dw2, 56, 1);
    ste.s2affd = extract64(dw2, 60, 1);

    ste.s2ttb = dw3 & S2TTB_ADDR_MASK;

    if (m_ste_cache.size() >= STE_CACHE_MAX && !m_ste_cache_lru.empty()) {
        uint32_t victim = m_ste_cache_lru.back();
        m_ste_cache_lru.pop_back();
        m_ste_cache.erase(victim);
    }
    m_ste_cache[sid] = ste;
    m_ste_cache_lru.push_front(sid);
    return ste.valid;
}

template <unsigned int BUSWIDTH>
bool smmuv3<BUSWIDTH>::read_cd(uint32_t sid, const STE& ste, uint32_t substream_id, CD& cd)
{
    uint64_t key = (static_cast<uint64_t>(sid) << 32) | substream_id;
    auto it = m_cd_cache.find(key);
    if (it != m_cd_cache.end()) {
        cd = it->second;
        return cd.valid;
    }

    constexpr uint32_t CD_TSZ_MASK = 0x3F;
    constexpr uint32_t CD_TG_MASK = 0x3;
    constexpr uint32_t CD_IPS_MASK = 0x7;
    constexpr uint32_t CD_ASID_MASK = 0xFFFF;
    constexpr uint32_t CD_VALID_SHIFT = 31;
    constexpr uint32_t CD_AARCH64_SHIFT = 9;
    constexpr uint32_t CD_T0_TG_SHIFT = 6;
    constexpr uint32_t CD_EPD0_SHIFT = 14;
    constexpr uint32_t CD_T1SZ_SHIFT = 16;
    constexpr uint32_t CD_T1_TG_SHIFT = 22;
    constexpr uint32_t CD_EPD1_SHIFT = 30;
    constexpr uint32_t CD_ASID_SHIFT = 16;
    constexpr uint32_t CD_TTB_HI_MASK = 0xFFFFF;
    constexpr uint32_t CD_TTB_LO_MASK = 0xFFFFFFF0;
    constexpr uint32_t CD_AFFD_BIT_IN_W1 = 3;
    constexpr uint32_t CD_HA_BIT_IN_W1 = 11;
    constexpr uint32_t CD_HD_BIT_IN_W1 = 10;

    uint64_t cd_addr;
    constexpr size_t L1CD_SIZE = 8;
    constexpr uint64_t L1CD_VALID_BIT = 1ULL;
    constexpr uint64_t L1CD_PTR_MASK = ~0xFFFULL;

    if (ste.s1fmt == S1FMT_LINEAR || ste.s1cdmax == 0) {
        cd_addr = ste.s1contextptr + (substream_id * SMMUV3_CD_SIZE);
    } else {
        uint32_t split = (ste.s1fmt == S1FMT_4KB_L2) ? S1FMT_4KB_L2_SPLIT : S1FMT_64KB_L2_SPLIT;
        uint64_t l1_idx = static_cast<uint64_t>(substream_id) >> split;
        uint64_t l2_idx = static_cast<uint64_t>(substream_id) & ((1ULL << split) - 1);
        uint64_t l1cd_addr = ste.s1contextptr + (l1_idx * L1CD_SIZE);
        uint64_t l1cd = 0;
        if (!dma_read(l1cd_addr, &l1cd, L1CD_SIZE)) {
            set_gerror(GERROR_BIT_SFM_ERR);
            return false;
        }
        if ((l1cd & L1CD_VALID_BIT) == 0) {
            cd.valid = false;
            return false;
        }
        uint64_t l2_ptr = l1cd & L1CD_PTR_MASK;
        cd_addr = l2_ptr + (l2_idx * SMMUV3_CD_SIZE);
    }

    std::array<uint8_t, SMMUV3_CD_SIZE> cd_buf{};

    if (!dma_read(cd_addr, cd_buf.data(), SMMUV3_CD_SIZE)) {
        set_gerror(GERROR_BIT_SFM_ERR);
        return false;
    }

    std::array<uint32_t, 6> w{};
    for (size_t i = 0; i < w.size(); ++i) {
        w[i] = load_le<uint32_t>(cd_buf.data() + i * sizeof(uint32_t));
    }

    cd.valid = (w[0] >> CD_VALID_SHIFT) & 0x1;
    cd.aarch64 = (w[1] >> CD_AARCH64_SHIFT) & 0x1;
    cd.tsz[0] = w[0] & CD_TSZ_MASK;
    cd.tg[0] = (w[0] >> CD_T0_TG_SHIFT) & CD_TG_MASK;
    cd.epd[0] = (w[0] >> CD_EPD0_SHIFT) & 0x1;
    cd.tsz[1] = (w[0] >> CD_T1SZ_SHIFT) & CD_TSZ_MASK;
    cd.tg[1] = (w[0] >> CD_T1_TG_SHIFT) & CD_TG_MASK;
    cd.epd[1] = (w[0] >> CD_EPD1_SHIFT) & 0x1;
    cd.ips = w[1] & CD_IPS_MASK;
    cd.asid = (w[1] >> CD_ASID_SHIFT) & CD_ASID_MASK;
    cd.affd = (w[1] >> CD_AFFD_BIT_IN_W1) & 0x1;
    cd.ha = (w[1] >> CD_HA_BIT_IN_W1) & 0x1;
    cd.hd = (w[1] >> CD_HD_BIT_IN_W1) & 0x1;

    cd.ttb[0] = (static_cast<uint64_t>(w[3] & CD_TTB_HI_MASK) << 32) | static_cast<uint64_t>(w[2] & CD_TTB_LO_MASK);
    cd.ttb[1] = (static_cast<uint64_t>(w[5] & CD_TTB_HI_MASK) << 32) | static_cast<uint64_t>(w[4] & CD_TTB_LO_MASK);

    if (m_cd_cache.size() >= CD_CACHE_MAX && !m_cd_cache_lru.empty()) {
        uint64_t victim = m_cd_cache_lru.back();
        m_cd_cache_lru.pop_back();
        m_cd_cache.erase(victim);
    }
    m_cd_cache[key] = cd;
    m_cd_cache_lru.push_front(key);
    return cd.valid;
}

template <unsigned int BUSWIDTH>
bool smmuv3<BUSWIDTH>::smmuv3_ptw64(PtwCtx& ctx, TransReq& req)
{
    unsigned int tsz;
    unsigned int inputsize;
    unsigned int outputsize;
    unsigned int grainsize = 0;
    unsigned int stride;
    int level = 0;
    unsigned int firstblocklevel = 0;
    unsigned int tg;
    unsigned int baselowerbound;
    bool blocktranslate = false;
    bool epd = false;
    uint64_t descmask;
    uint64_t ttbr;
    uint64_t desc;
    uint32_t attrs;
    uint32_t s2attrs;

    req.err = false;
    req.tableattrs = 0;
    req.fault_level = 0;

    constexpr uint32_t VA_BITS_TOTAL = 64;
    constexpr uint32_t STRIDE_OFFSET = 3;
    constexpr uint32_t LEVEL_LEAF = 3;
    constexpr uint32_t FIRSTBLOCK_LARGE_GRAIN = 2;
    constexpr uint32_t FIRSTBLOCK_4K = 1;
    constexpr uint32_t TG1_INVALID = 0xFF;

    if (ctx.stage == 1) {
        if ((ctx.iova & (1ULL << VA_SIGN_BIT_SHIFT)) == 0) {
            ttbr = ctx.ttb[0];
            tsz = ctx.tsz[0];
            tg = ctx.tg[0];
            epd = ctx.epd[0];
        } else {
            ttbr = ctx.ttb[1];
            tsz = ctx.tsz[1];
            epd = ctx.epd[1];
            switch (ctx.tg[1]) {
            case 1:
                tg = CD_TG_16K;
                break;
            case 2:
                tg = CD_TG_4K;
                break;
            case 3:
                tg = CD_TG_64K;
                break;
            default:
                tg = TG1_INVALID;
                break;
            }
        }
    } else {
        ttbr = ctx.ttb[0];
        tsz = ctx.tsz[0];
        tg = ctx.tg[0];
        epd = ctx.epd[0];
    }

    if (epd) {
        req.event_type = SMMUV3_EVT_F_TRANSLATION;
        req.err = true;
        return false;
    }

    inputsize = VA_BITS_TOTAL - tsz;

    switch (tg) {
    case CD_TG_64K:
        grainsize = GRAINSIZE_64K;
        level = LEVEL_LEAF;
        firstblocklevel = FIRSTBLOCK_LARGE_GRAIN;
        break;
    case CD_TG_16K:
        grainsize = GRAINSIZE_16K;
        level = LEVEL_LEAF;
        firstblocklevel = FIRSTBLOCK_LARGE_GRAIN;
        break;
    case CD_TG_4K:
        grainsize = GRAINSIZE_4K;
        level = 2;
        firstblocklevel = FIRSTBLOCK_4K;
        break;
    default:
        SCP_ERR(()) << "Invalid TG value: " << tg;
        req.event_type = SMMUV3_EVT_F_ADDR_SIZE;
        req.err = true;
        return false;
    }

    outputsize = OUTPUT_SIZE_MAP[ctx.ips];
    if (outputsize > p_pamax) outputsize = p_pamax;

    stride = grainsize - STRIDE_OFFSET;

    if (ctx.stage == 1) {
        if (grainsize < GRAINSIZE_64K && (inputsize > (grainsize + 3 * stride)))
            level = 0;
        else if (inputsize > (grainsize + 2 * stride))
            level = 1;
        else if (inputsize > (grainsize + stride))
            level = 2;

        if (inputsize < INPUT_SIZE_MIN || inputsize > INPUT_SIZE_MAX ||
            extract64(ctx.iova, inputsize, VA_BITS_TOTAL - inputsize)) {
            req.event_type = SMMUV3_EVT_F_ADDR_SIZE;
            req.err = true;
            return false;
        }
    } else {
        unsigned int startlevel = ctx.sl0;
        level = 3 - startlevel;
        if (grainsize == GRAINSIZE_4K) level = 2 - startlevel;
        if (!check_s2_startlevel(outputsize, level, inputsize, stride)) {
            req.event_type = SMMUV3_EVT_F_ADDR_SIZE;
            req.err = true;
            return false;
        }
    }

    constexpr uint64_t INDEX_ALIGN_8B = 7ULL;

    unsigned int level_coverage = grainsize + (LEVEL_LEAF - level + 1) * stride;
    unsigned int concat_bits = 0;
    if (ctx.stage == 2 && inputsize > level_coverage) {
        concat_bits = inputsize - level_coverage;
        if (concat_bits > 4) {
            req.event_type = SMMUV3_EVT_F_ADDR_SIZE;
            req.err = true;
            return false;
        }
    }

    baselowerbound = STRIDE_OFFSET + inputsize - ((LEVEL_LEAF - level) * stride + grainsize);
    ttbr = extract64(ttbr, 0, PA_BITS);
    ttbr &= ~((1ULL << baselowerbound) - 1);

    if (concat_bits > 0) {
        uint64_t concat_idx = (ctx.iova >> level_coverage) & ((1ULL << concat_bits) - 1);
        uint64_t table_bytes = 1ULL << (grainsize + stride);
        ttbr |= concat_idx * table_bytes;
    }

    if (!check_out_addr(ttbr, outputsize)) {
        req.event_type = SMMUV3_EVT_F_ADDR_SIZE;
        req.err = true;
        return false;
    }

    descmask = (1ULL << grainsize) - 1;

    do {
        unsigned int addrselectbottom = (LEVEL_LEAF - level) * stride + grainsize;
        uint64_t index = (ctx.iova >> (addrselectbottom - STRIDE_OFFSET)) & descmask;
        index &= ~INDEX_ALIGN_8B;
        uint64_t descaddr = ttbr | index;

        if (ctx.stage == 1 && ctx.s2_enabled) {
            PtwCtx s2ctx;
            s2ctx.ttb[0] = ctx.ste->s2ttb;
            s2ctx.tsz[0] = ctx.ste->s2t0sz;
            s2ctx.tg[0] = ctx.ste->s2tg;
            s2ctx.ips = ctx.ste->s2ps;
            s2ctx.sl0 = ctx.ste->s2sl0;
            s2ctx.epd[0] = false;
            s2ctx.stage = 2;
            s2ctx.iova = descaddr;
            s2ctx.access = IOMMUAccessFlags::RO;
            s2ctx.s2_enabled = false;
            s2ctx.ste = ctx.ste;
            s2ctx.affd = ctx.ste->s2affd;
            s2ctx.ha = false;
            s2ctx.hd = ctx.ste->s2hd;

            TransReq s2req;
            s2req.iova = descaddr;
            s2req.access = IOMMUAccessFlags::RO;
            s2req.pa = descaddr;

            if (!smmuv3_ptw64(s2ctx, s2req) || s2req.err) {
                req.event_type = SMMUV3_EVT_F_TRANSLATION;
                req.fault_level = level;
                req.err = true;
                return false;
            }
            descaddr = s2req.pa;
        }

        if (!dma_read(descaddr, &desc, sizeof(desc))) {
            req.event_type = SMMUV3_EVT_F_WALK_EABT;
            req.fault_level = level;
            req.err = true;
            return false;
        }

        req.leaf_desc_addr = descaddr;

        constexpr uint32_t DESC_TYPE_MASK = 0x3;
        constexpr uint32_t TABLE_ATTRS_SHIFT = 59;
        constexpr uint32_t TABLE_ATTRS_WIDTH = 5;

        unsigned int type = desc & DESC_TYPE_MASK;
        SCP_INFO(()) << "S" << ctx.stage << " L" << level << " iova=0x" << std::hex << ctx.iova << " descaddr=0x"
                     << descaddr << " desc=0x" << desc << " type=" << type;

        ttbr = extract64(desc, 0, PA_BITS);
        ttbr &= ~descmask;

        if (level == LEVEL_LEAF) {
            if (type != DESC_TYPE_TABLE_OR_PAGE) {
                req.event_type = SMMUV3_EVT_F_TRANSLATION;
                req.fault_level = level;
                req.err = true;
                return false;
            }
            break;
        }

        switch (type) {
        case DESC_TYPE_RESERVED:
        case DESC_TYPE_INVALID:
            req.event_type = SMMUV3_EVT_F_TRANSLATION;
            req.fault_level = level;
            req.err = true;
            return false;

        case DESC_TYPE_BLOCK:
            blocktranslate = true;
            if (level < (int)firstblocklevel) {
                req.event_type = SMMUV3_EVT_F_TRANSLATION;
                req.fault_level = level;
                req.err = true;
                return false;
            }
            break;

        case DESC_TYPE_TABLE_OR_PAGE:
            req.tableattrs |= extract64(desc, TABLE_ATTRS_SHIFT, TABLE_ATTRS_WIDTH);
            if (!check_out_addr(ttbr, outputsize)) {
                req.event_type = SMMUV3_EVT_F_ADDR_SIZE;
                req.fault_level = level;
                req.err = true;
                return false;
            }
            level++;
            break;

        default:
            SCP_ERR(()) << "SMMUv3: unreachable descriptor type " << type;
            req.event_type = SMMUV3_EVT_F_TRANSLATION;
            req.fault_level = level;
            req.err = true;
            return false;
        }
    } while (!blocktranslate);

    if (!check_out_addr(ttbr, outputsize)) {
        req.event_type = SMMUV3_EVT_F_ADDR_SIZE;
        req.fault_level = level;
        req.err = true;
        return false;
    }

    constexpr uint32_t LEVEL_COUNT = 4;
    constexpr uint32_t DESC_ATTRS_LOW_SHIFT = 2;
    constexpr uint32_t DESC_ATTRS_LOW_WIDTH = 10;
    constexpr uint32_t DESC_ATTRS_HI_SHIFT = 52;
    constexpr uint32_t DESC_ATTRS_HI_WIDTH = 12;
    constexpr uint32_t TABLE_AP_SHIFT_IN_ATTRS = 11;
    constexpr uint32_t TABLE_PXN_BIT_IN_TABLEATTRS = 3;

    unsigned long page_size = (1ULL << ((stride * (LEVEL_COUNT - level)) + STRIDE_OFFSET));
    ttbr |= (ctx.iova & (page_size - 1));
    req.page_size = ((stride * (LEVEL_COUNT - level)) + STRIDE_OFFSET);
    req.pa = ttbr;

    req.descriptor = desc;

    s2attrs = attrs = extract64(desc, DESC_ATTRS_LOW_SHIFT, DESC_ATTRS_LOW_WIDTH) |
                      (extract64(desc, DESC_ATTRS_HI_SHIFT, DESC_ATTRS_HI_WIDTH) << DESC_ATTRS_LOW_WIDTH);

    if (ctx.stage == 1) {
        attrs |= extract32(req.tableattrs, 0, 2) << TABLE_AP_SHIFT_IN_ATTRS;
        attrs |= extract32(req.tableattrs, TABLE_PXN_BIT_IN_TABLEATTRS, 1) << DESC_ATTR_AP2_BIT;
    }

    req.prot = IOMMUAccessFlags::RW;

    bool dbm_set = (desc >> DESC_BIT_DBM) & 1ULL;
    bool stage1_httu_af = (ctx.stage == 1) ? ctx.ha : false;
    bool stage1_httu_dbm = (ctx.stage == 1) ? ctx.hd : false;
    bool stage2_httu_af = (ctx.stage == 2) && ctx.ste && ctx.ste->s2hd;
    (void)stage2_httu_af;

    bool af_set = attrs & (1u << DESC_ATTR_AF_BIT);
    if (!af_set) {
        bool affd = (ctx.stage == 1) ? ctx.affd : (ctx.ste ? ctx.ste->s2affd : false);
        bool httu_af = stage1_httu_af || stage2_httu_af;
        if (httu_af) {
            uint64_t new_desc = desc | (1ULL << DESC_BIT_AF);
            if (dma_write(req.leaf_desc_addr, &new_desc, sizeof(new_desc))) {
                desc = new_desc;
                attrs |= (1u << DESC_ATTR_AF_BIT);
                af_set = true;
            }
        }
        if (!af_set && !affd) {
            req.event_type = SMMUV3_EVT_F_ACCESS;
            req.fault_level = level;
            req.err = true;
            return false;
        }
    }

    if (ctx.stage == 1) {
        if (!(attrs & (1u << DESC_ATTR_AP1_BIT))) {
            req.event_type = SMMUV3_EVT_F_PERMISSION;
            req.fault_level = level;
            req.err = true;
            return false;
        }
        if (attrs & (1u << DESC_ATTR_AP2_BIT)) {
            if (ctx.access == IOMMUAccessFlags::WO) {
                if (stage1_httu_dbm && dbm_set) {
                    uint64_t new_desc = desc & ~(1ULL << (DESC_ATTRS_LOW_SHIFT + DESC_ATTR_AP2_BIT));
                    if (dma_write(req.leaf_desc_addr, &new_desc, sizeof(new_desc))) {
                        desc = new_desc;
                        attrs &= ~(1u << DESC_ATTR_AP2_BIT);
                    }
                }
                if (attrs & (1u << DESC_ATTR_AP2_BIT)) {
                    req.event_type = SMMUV3_EVT_F_PERMISSION;
                    req.fault_level = level;
                    req.err = true;
                    return false;
                }
            } else {
                req.prot &= ~IOMMUAccessFlags::WO;
            }
        }
    } else {
        switch ((s2attrs >> DESC_ATTR_AP1_BIT) & 0x3) {
        case S2AP_NONE:
            req.event_type = SMMUV3_EVT_F_PERMISSION;
            req.fault_level = level;
            req.err = true;
            return false;
        case S2AP_RO:
            if (ctx.access == IOMMUAccessFlags::WO) {
                req.event_type = SMMUV3_EVT_F_PERMISSION;
                req.fault_level = level;
                req.err = true;
                return false;
            }
            req.prot &= ~IOMMUAccessFlags::WO;
            break;
        case S2AP_WO:
            if (ctx.access == IOMMUAccessFlags::RO) {
                req.event_type = SMMUV3_EVT_F_PERMISSION;
                req.fault_level = level;
                req.err = true;
                return false;
            }
            req.prot &= ~IOMMUAccessFlags::RO;
            break;
        case S2AP_RW:
            break;
        default:
            SCP_ERR(()) << "SMMUv3: unreachable S2AP value";
            req.event_type = SMMUV3_EVT_F_PERMISSION;
            req.fault_level = level;
            req.err = true;
            return false;
        }
    }

    SCP_INFO(()) << "PTW success: 0x" << std::hex << ctx.iova << " -> 0x" << req.pa
                 << " prot=" << static_cast<uint32_t>(req.prot) << " page_size=" << std::dec << req.page_size;
    return true;
}

template <unsigned int BUSWIDTH>
typename smmuv3<BUSWIDTH>::IOMMUTLBEntry smmuv3<BUSWIDTH>::smmuv3_translate(tlm::tlm_generic_payload& txn, uint32_t sid,
                                                                            uint32_t substream_id, bool is_secure,
                                                                            bool is_ats_tr)
{
    (void)is_secure;
    (void)is_ats_tr;

    uint64_t iova = txn.get_address();
    IOMMUTLBEntry ret = {
        .iova = iova,
        .translated_addr = iova,
        .addr_mask = (1ULL << PAGE_SHIFT_4K) - 1,
        .perm = IOMMUAccessFlags::RW,
        .descriptor = 0,
        .table_attrs = 0,
        .fault_type = 0,
        .fault_level = 0,
        .stallable_fault = false,
    };

    if (static_cast<uint32_t>(CR0[CR0_SMMUEN]) == 0) {
        ret.addr_mask = -1ULL;
        return ret;
    }

    TransReq req;
    req.iova = iova;
    req.sid = sid;
    req.substream_id = substream_id;
    req.access = (txn.get_command() == tlm::TLM_WRITE_COMMAND) ? IOMMUAccessFlags::WO : IOMMUAccessFlags::RO;

    if (!find_ste(sid, req.ste) || !req.ste.valid) {
        if (static_cast<uint32_t>(GBPA[GBPA_ABORT])) {
            ret.perm = IOMMUAccessFlags::NONE;
            ret.fault_type = SMMUV3_EVT_F_STE_FETCH;
            record_event_full(SMMUV3_EVT_F_STE_FETCH, sid, substream_id, iova, txn, 0, 0, 0, false);
            return ret;
        } else {
            ret.addr_mask = -1ULL;
            return ret;
        }
    }

    if (req.ste.s1cdmax > 0 && substream_id >= (1u << req.ste.s1cdmax)) {
        ret.perm = IOMMUAccessFlags::NONE;
        ret.fault_type = SMMUV3_EVT_C_BAD_SUBSTREAMID;
        record_event_full(SMMUV3_EVT_C_BAD_SUBSTREAMID, sid, substream_id, iova, txn, 0, 0, 0, false);
        return ret;
    }

    // Tag every IOTLB key for this transaction with the resolved CPU translation regime so cached
    // entries cannot alias across regimes (NS-EL1 / NS-EL2 / NS-EL2-E2H / Secure).
    const bool e2h_enabled = static_cast<uint32_t>(CR2[CR2_E2H]) != 0;
    const uint8_t iotlb_regime = compute_iotlb_regime(req.ste.strw, e2h_enabled, is_secure);

    constexpr uint8_t IOTLB_LEAF_LEVEL = 3;

    switch (req.ste.config) {
    case STE_CONFIG_ABORT:
        ret.perm = IOMMUAccessFlags::NONE;
        ret.fault_type = SMMUV3_EVT_C_BAD_STE;
        record_event_full(SMMUV3_EVT_C_BAD_STE, sid, substream_id, iova, txn, 0, 0, 0, false);
        return ret;
    case STE_CONFIG_BYPASS:
        ret.addr_mask = -1ULL;
        return ret;
    case STE_CONFIG_S1:
        if (!read_cd(sid, req.ste, substream_id, req.cd) || !req.cd.valid) {
            ret.perm = IOMMUAccessFlags::NONE;
            ret.fault_type = SMMUV3_EVT_F_CD_FETCH;
            record_event_full(SMMUV3_EVT_F_CD_FETCH, sid, substream_id, iova, txn, SMMUV3_CLASS_CD, 0, 0, false);
            return ret;
        }
        {
            IOTLBKey lookup{ req.ste.vmid,
                             req.cd.asid,
                             iova & ~((1ULL << PAGE_SHIFT_4K) - 1),
                             static_cast<uint8_t>(req.cd.tg[0]),
                             IOTLB_LEAF_LEVEL,
                             is_secure,
                             iotlb_regime };
            if (iotlb_lookup(lookup, ret)) {
                return ret;
            }
        }
        {
            PtwCtx ctx;
            ctx.ttb[0] = req.cd.ttb[0];
            ctx.ttb[1] = req.cd.ttb[1];
            ctx.tsz[0] = req.cd.tsz[0];
            ctx.tsz[1] = req.cd.tsz[1];
            ctx.tg[0] = req.cd.tg[0];
            ctx.tg[1] = req.cd.tg[1];
            ctx.ips = req.cd.ips;
            ctx.epd[0] = req.cd.epd[0];
            ctx.epd[1] = req.cd.epd[1];
            ctx.stage = 1;
            ctx.iova = iova;
            ctx.access = req.access;
            ctx.s2_enabled = false;
            ctx.ste = nullptr;
            ctx.affd = req.cd.affd;
            ctx.ha = req.cd.ha;
            ctx.hd = req.cd.hd;
            smmuv3_ptw64(ctx, req);
        }
        break;
    case STE_CONFIG_S2: {
        IOTLBKey lookup{ req.ste.vmid,
                         0,
                         iova & ~((1ULL << PAGE_SHIFT_4K) - 1),
                         static_cast<uint8_t>(req.ste.s2tg),
                         IOTLB_LEAF_LEVEL,
                         is_secure,
                         iotlb_regime };
        if (iotlb_lookup(lookup, ret)) {
            return ret;
        }
        PtwCtx ctx;
        ctx.ttb[0] = req.ste.s2ttb;
        ctx.tsz[0] = req.ste.s2t0sz;
        ctx.tg[0] = req.ste.s2tg;
        ctx.ips = req.ste.s2ps;
        ctx.sl0 = req.ste.s2sl0;
        ctx.epd[0] = false;
        ctx.stage = 2;
        ctx.iova = iova;
        ctx.access = req.access;
        ctx.s2_enabled = false;
        ctx.ste = &req.ste;
        ctx.affd = req.ste.s2affd;
        ctx.ha = false;
        ctx.hd = req.ste.s2hd;
        smmuv3_ptw64(ctx, req);
    } break;
    case STE_CONFIG_NESTED:
        if (!read_cd(sid, req.ste, substream_id, req.cd) || !req.cd.valid) {
            ret.perm = IOMMUAccessFlags::NONE;
            ret.fault_type = SMMUV3_EVT_F_CD_FETCH;
            record_event_full(SMMUV3_EVT_F_CD_FETCH, sid, substream_id, iova, txn, SMMUV3_CLASS_CD, 0, 0, false);
            return ret;
        }
        {
            IOTLBKey lookup{ req.ste.vmid,
                             req.cd.asid,
                             iova & ~((1ULL << PAGE_SHIFT_4K) - 1),
                             static_cast<uint8_t>(req.cd.tg[0]),
                             IOTLB_LEAF_LEVEL,
                             is_secure,
                             iotlb_regime };
            if (iotlb_lookup(lookup, ret)) {
                return ret;
            }
        }
        {
            PtwCtx ctx;
            ctx.ttb[0] = req.cd.ttb[0];
            ctx.ttb[1] = req.cd.ttb[1];
            ctx.tsz[0] = req.cd.tsz[0];
            ctx.tsz[1] = req.cd.tsz[1];
            ctx.tg[0] = req.cd.tg[0];
            ctx.tg[1] = req.cd.tg[1];
            ctx.ips = req.cd.ips;
            ctx.epd[0] = req.cd.epd[0];
            ctx.epd[1] = req.cd.epd[1];
            ctx.stage = 1;
            ctx.iova = iova;
            ctx.access = req.access;
            ctx.s2_enabled = true;
            ctx.ste = &req.ste;
            ctx.affd = req.cd.affd;
            ctx.ha = req.cd.ha;
            ctx.hd = req.cd.hd;
            smmuv3_ptw64(ctx, req);

            if (!req.err) {
                uint64_t s1_pa = req.pa;
                PtwCtx s2ctx;
                s2ctx.ttb[0] = req.ste.s2ttb;
                s2ctx.tsz[0] = req.ste.s2t0sz;
                s2ctx.tg[0] = req.ste.s2tg;
                s2ctx.ips = req.ste.s2ps;
                s2ctx.sl0 = req.ste.s2sl0;
                s2ctx.epd[0] = false;
                s2ctx.stage = 2;
                s2ctx.iova = s1_pa;
                s2ctx.access = req.access;
                s2ctx.s2_enabled = false;
                s2ctx.ste = &req.ste;
                s2ctx.affd = req.ste.s2affd;
                s2ctx.ha = false;
                s2ctx.hd = req.ste.s2hd;
                smmuv3_ptw64(s2ctx, req);
            }
        }
        break;
    default:
        ret.perm = IOMMUAccessFlags::NONE;
        ret.fault_type = SMMUV3_EVT_C_BAD_STE;
        record_event_full(SMMUV3_EVT_C_BAD_STE, sid, substream_id, iova, txn, 0, 0, 0, false);
        return ret;
    }

    if (req.err) {
        ret.perm = IOMMUAccessFlags::NONE;
        ret.fault_type = req.event_type;
        ret.fault_level = req.fault_level;
        bool stallable = (req.event_type == SMMUV3_EVT_F_TRANSLATION || req.event_type == SMMUV3_EVT_F_PERMISSION ||
                          req.event_type == SMMUV3_EVT_F_ACCESS || req.event_type == SMMUV3_EVT_F_ADDR_SIZE);
        ret.stallable_fault = stallable && (req.ste.config == STE_CONFIG_S1 || req.ste.config == STE_CONFIG_NESTED) &&
                              !req.ste.s1stalld;
        uint32_t class_ = (req.ste.config == STE_CONFIG_S2) ? SMMUV3_CLASS_IN : SMMUV3_CLASS_TT;
        if (!ret.stallable_fault) {
            record_event_full(req.event_type, sid, substream_id, iova, txn, class_, req.fault_level, 0, false);
        }
        return ret;
    }

    ret.translated_addr = req.pa;
    ret.perm = req.prot;
    ret.addr_mask = (1ULL << req.page_size) - 1;
    ret.descriptor = req.descriptor;
    ret.table_attrs = req.tableattrs;

    IOTLBKey key;
    key.vmid = req.ste.vmid;
    key.iova = iova & ~ret.addr_mask;
    key.level = IOTLB_LEAF_LEVEL;
    key.secure = is_secure;
    key.regime = iotlb_regime;

    if (req.ste.config == STE_CONFIG_S1 || req.ste.config == STE_CONFIG_NESTED) {
        key.asid = req.cd.asid;
        key.tg = static_cast<uint8_t>(req.cd.tg[0]);
    } else {
        key.asid = 0;
        key.tg = static_cast<uint8_t>(req.ste.s2tg);
    }
    iotlb_insert(key, ret);

    return ret;
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::iotlb_insert(const IOTLBKey& key, const IOMMUTLBEntry& entry)
{
    if (m_iotlb.find(key) != m_iotlb.end()) {
        m_iotlb_lru.remove(key);
    } else if (m_iotlb.size() >= p_iotlb_size.get_value()) {
        auto lru_key = m_iotlb_lru.back();
        m_iotlb_lru.pop_back();
        m_iotlb.erase(lru_key);
    }
    m_iotlb[key] = entry;
    m_iotlb_lru.push_front(key);
}

template <unsigned int BUSWIDTH>
bool smmuv3<BUSWIDTH>::iotlb_lookup(const IOTLBKey& key, IOMMUTLBEntry& entry)
{
    auto it = m_iotlb.find(key);
    if (it != m_iotlb.end()) {
        entry = it->second;
        return true;
    }
    return false;
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::iotlb_inv_all()
{
    m_iotlb.clear();
    m_iotlb_lru.clear();
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::iotlb_inv_asid(uint16_t asid)
{
    for (auto it = m_iotlb.begin(); it != m_iotlb.end();) {
        if (it->first.asid == asid) {
            m_iotlb_lru.remove(it->first);
            it = m_iotlb.erase(it);
        } else {
            ++it;
        }
    }
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::iotlb_inv_vmid(uint16_t vmid)
{
    for (auto it = m_iotlb.begin(); it != m_iotlb.end();) {
        if (it->first.vmid == vmid) {
            m_iotlb_lru.remove(it->first);
            it = m_iotlb.erase(it);
        } else {
            ++it;
        }
    }
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::iotlb_inv_iova(uint16_t vmid, uint16_t asid, uint64_t iova, uint8_t tg, uint64_t addr_mask)
{
    uint64_t iova_masked = iova & ~addr_mask;
    for (auto it = m_iotlb.begin(); it != m_iotlb.end();) {
        uint64_t entry_masked = it->first.iova & ~addr_mask;
        if (it->first.vmid == vmid && it->first.asid == asid && entry_masked == iova_masked && it->first.tg == tg) {
            m_iotlb_lru.remove(it->first);
            it = m_iotlb.erase(it);
        } else {
            ++it;
        }
    }
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::consume_cmdq()
{
    constexpr uint64_t CMDQ_BASE_LOG2_BITS_MASK = 0x1FULL;

    uint64_t hi = static_cast<uint64_t>(CMDQ_BASE_HI) & CMDQ_BASE_HI_MASK;
    uint64_t lo = static_cast<uint64_t>(CMDQ_BASE_LO) & ~CMDQ_BASE_LOG2_BITS_MASK;
    uint64_t base = (hi << 32) | lo;
    uint32_t log2size = clamp_queue_log2size(static_cast<uint32_t>(CMDQ_BASE_LO[CMDQ_BASE_LOG2SIZE]));
    if (log2size == 0) return;

    uint32_t prod = static_cast<uint32_t>(CMDQ_PROD);
    uint32_t cons = static_cast<uint32_t>(CMDQ_CONS);
    uint32_t wrap_mask = (1u << (log2size + 1)) - 1;
    uint32_t idx_mask = (1u << log2size) - 1;

    SCP_INFO(()) << "SMMUv3: consume_cmdq base=0x" << std::hex << base << " log2size=" << std::dec << log2size
                 << " prod=0x" << std::hex << prod << " cons=0x" << cons;

    while ((prod & wrap_mask) != (cons & wrap_mask)) {
        uint32_t idx = cons & idx_mask;
        uint64_t cmd_addr = base + (idx * SMMUV3_CMD_SIZE);
        std::array<uint8_t, SMMUV3_CMD_SIZE> cmd{};

        if (!dma_read(cmd_addr, cmd.data(), SMMUV3_CMD_SIZE)) {
            SCP_WARN(()) << "SMMUv3: dma_read failed at 0x" << std::hex << cmd_addr;
            CMDQ_CONS = cons;
            set_cmdq_err(SMMUV3_CERROR_ABT);
            return;
        }

        uint8_t opcode = cmd[0];
        SCP_INFO(()) << "SMMUv3: cmd opcode=0x" << std::hex << (int)opcode << " at idx=" << std::dec << idx;

        switch (opcode) {
        case CMD_OP_PREFETCH_CONFIG:
            handle_cmd_prefetch_config(cmd.data());
            break;
        case CMD_OP_PREFETCH_ADDR:
            handle_cmd_prefetch_addr(cmd.data());
            break;
        case CMD_OP_CFGI_STE:
            handle_cmd_cfgi_ste(cmd.data());
            break;
        case CMD_OP_CFGI_STE_RANGE:
            handle_cmd_cfgi_ste_range(cmd.data());
            break;
        case CMD_OP_CFGI_CD:
            handle_cmd_cfgi_cd(cmd.data());
            break;
        case CMD_OP_CFGI_CD_ALL:
            handle_cmd_cfgi_cd_all(cmd.data());
            break;
        case CMD_OP_CFGI_ALL:
            handle_cmd_cfgi_all(cmd.data());
            break;
        case CMD_OP_TLBI_NH_ALL:
            handle_cmd_tlbi_nh_all(cmd.data());
            break;
        case CMD_OP_TLBI_NH_ASID:
            handle_cmd_tlbi_nh_asid(cmd.data());
            break;
        case CMD_OP_TLBI_NH_VA:
            handle_cmd_tlbi_nh_va(cmd.data());
            break;
        case CMD_OP_TLBI_NH_VAA:
            handle_cmd_tlbi_nh_vaa(cmd.data());
            break;
        case CMD_OP_TLBI_EL3_ALL:
            handle_cmd_tlbi_nh_all(cmd.data());
            break;
        case CMD_OP_TLBI_EL3_VA:
            handle_cmd_tlbi_nh_va(cmd.data());
            break;
        case CMD_OP_TLBI_EL2_ALL:
            handle_cmd_tlbi_nh_all(cmd.data());
            break;
        case CMD_OP_TLBI_EL2_ASID:
            handle_cmd_tlbi_nh_asid(cmd.data());
            break;
        case CMD_OP_TLBI_EL2_VA:
            handle_cmd_tlbi_nh_va(cmd.data());
            break;
        case CMD_OP_TLBI_EL2_VAA:
            handle_cmd_tlbi_nh_vaa(cmd.data());
            break;
        case CMD_OP_TLBI_S12_VMALL:
            handle_cmd_tlbi_s12_vmall(cmd.data());
            break;
        case CMD_OP_TLBI_S2_IPA:
            handle_cmd_tlbi_s2_ipa(cmd.data());
            break;
        case CMD_OP_TLBI_NSNH_ALL:
            handle_cmd_tlbi_nsnh_all(cmd.data());
            break;
        case CMD_OP_ATC_INV:
            handle_cmd_atc_inv(cmd.data());
            break;
        case CMD_OP_PRI_RESP:
            handle_cmd_pri_resp(cmd.data());
            break;
        case CMD_OP_RESUME:
            handle_cmd_resume(cmd.data());
            break;
        case CMD_OP_STALL_TERM:
            break;
        case CMD_OP_SYNC:
            handle_cmd_sync(cmd.data());
            break;
        default:
            SCP_WARN(()) << "SMMUv3: unknown opcode 0x" << std::hex << (int)opcode;
            CMDQ_CONS = cons;
            set_cmdq_err(SMMUV3_CERROR_ILL);
            return;
        }

        cons = (cons + 1) & wrap_mask;
    }

    CMDQ_CONS = cons;
    SCP_INFO(()) << "SMMUv3: consume_cmdq done, CMDQ_CONS=0x" << std::hex << cons;
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::consume_secure_cmdq()
{
    constexpr uint64_t CMDQ_BASE_LOG2_BITS_MASK = 0x1FULL;

    uint64_t hi = static_cast<uint64_t>(S_CMDQ_BASE_HI) & CMDQ_BASE_HI_MASK;
    uint64_t lo = static_cast<uint64_t>(S_CMDQ_BASE_LO) & ~CMDQ_BASE_LOG2_BITS_MASK;
    uint64_t base = (hi << 32) | lo;
    uint32_t log2size = clamp_queue_log2size(static_cast<uint32_t>(S_CMDQ_BASE_LO[S_CMDQ_BASE_LOG2SIZE]));
    if (log2size == 0) return;

    uint32_t prod = static_cast<uint32_t>(S_CMDQ_PROD);
    uint32_t cons = static_cast<uint32_t>(S_CMDQ_CONS);
    uint32_t wrap_mask = (1u << (log2size + 1)) - 1;
    uint32_t idx_mask = (1u << log2size) - 1;

    while ((prod & wrap_mask) != (cons & wrap_mask)) {
        uint32_t idx = cons & idx_mask;
        uint64_t cmd_addr = base + (idx * SMMUV3_CMD_SIZE);
        std::array<uint8_t, SMMUV3_CMD_SIZE> cmd{};

        if (!dma_read(cmd_addr, cmd.data(), SMMUV3_CMD_SIZE)) {
            S_CMDQ_CONS = cons;
            set_secure_cmdq_err(SMMUV3_CERROR_ABT);
            return;
        }

        constexpr size_t CMD_SID_OFFSET = 4;
        constexpr size_t CMD_WORD1_OFFSET = 8;
        constexpr uint32_t CMD_SYNC_CS_SHIFT_LOCAL = 12;
        constexpr uint32_t CMD_SYNC_CS_MASK_LOCAL = 0x3;

        uint8_t opcode = cmd[0];
        switch (opcode) {
        case CMD_OP_PREFETCH_CONFIG:
        case CMD_OP_PREFETCH_ADDR:
            break;
        case CMD_OP_CFGI_STE: {
            uint32_t sid = load_le<uint32_t>(cmd.data() + CMD_SID_OFFSET);
            m_ste_cache_secure.erase(sid);
            break;
        }
        case CMD_OP_CFGI_STE_RANGE:
            m_ste_cache_secure.clear();
            break;
        case CMD_OP_CFGI_CD:
        case CMD_OP_CFGI_CD_ALL:
            m_cd_cache_secure.clear();
            break;
        case CMD_OP_CFGI_ALL:
            m_ste_cache_secure.clear();
            m_cd_cache_secure.clear();
            break;
        case CMD_OP_TLBI_NSNH_ALL:
        case CMD_OP_TLBI_NH_ALL:
        case CMD_OP_TLBI_NH_ASID:
        case CMD_OP_TLBI_NH_VA:
        case CMD_OP_TLBI_NH_VAA:
        case CMD_OP_TLBI_S12_VMALL:
        case CMD_OP_TLBI_S2_IPA:
            iotlb_inv_all();
            break;
        case CMD_OP_SYNC: {
            uint64_t dw0 = load_le<uint64_t>(cmd.data());
            uint64_t dw1 = load_le<uint64_t>(cmd.data() + CMD_WORD1_OFFSET);
            uint32_t cs = (static_cast<uint32_t>(dw0) >> CMD_SYNC_CS_SHIFT_LOCAL) & CMD_SYNC_CS_MASK_LOCAL;
            if (cs == SYNC_CS_IRQ) {
                constexpr uint64_t CMD_SYNC_MSIADDR_MASK = 0x000FFFFFFFFFFFFCULL;
                constexpr uint32_t CMD_SYNC_MSIDATA_SHIFT = 32;
                uint64_t msiaddr = dw1 & CMD_SYNC_MSIADDR_MASK;
                uint32_t msidata = static_cast<uint32_t>(dw0 >> CMD_SYNC_MSIDATA_SHIFT);
                if (msiaddr != 0) {
                    if (!dma_write(msiaddr, &msidata, sizeof(msidata))) {
                        set_secure_gerror(GERROR_BIT_MSI_CMDQ_ABT);
                    }
                }
            }
            break;
        }
        default:
            S_CMDQ_CONS = cons;
            set_secure_cmdq_err(SMMUV3_CERROR_ILL);
            return;
        }

        cons = (cons + 1) & wrap_mask;
    }

    S_CMDQ_CONS = cons;
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::record_secure_event(uint32_t type, uint32_t sid, uint64_t iova, uint32_t info)
{
    if (static_cast<uint32_t>(S_CR0[S_CR0_EVENTQEN]) == 0) return;

    constexpr uint64_t QUEUE_BASE_LOG2_BITS = 5;
    constexpr size_t EVT_SID_OFFSET = 4;
    constexpr size_t EVT_IOVA_OFFSET = 8;
    constexpr size_t EVT_INFO_OFFSET = 16;

    uint64_t base = (static_cast<uint64_t>(S_EVENTQ_BASE_HI) << 32) |
                    (static_cast<uint64_t>(S_EVENTQ_BASE_LO) & ~((1ULL << QUEUE_BASE_LOG2_BITS) - 1));
    uint32_t log2size = clamp_queue_log2size(static_cast<uint32_t>(S_EVENTQ_BASE_LO[S_EVENTQ_BASE_LOG2SIZE]));
    uint32_t prod = static_cast<uint32_t>(S_EVENTQ_PROD);
    uint32_t cons = static_cast<uint32_t>(S_EVENTQ_CONS);

    if (queue_full(prod, cons, log2size)) {
        set_secure_gerror(GERROR_BIT_EVENTQ_ABT);
        return;
    }

    uint32_t idx = prod & ((1u << log2size) - 1);
    uint64_t evt_addr = base + (idx * SMMUV3_EVENT_SIZE);
    std::array<uint8_t, SMMUV3_EVENT_SIZE> evt{};

    evt[0] = type;
    store_le<uint32_t>(evt.data() + EVT_SID_OFFSET, sid);
    store_le<uint64_t>(evt.data() + EVT_IOVA_OFFSET, iova);
    store_le<uint32_t>(evt.data() + EVT_INFO_OFFSET, info);

    if (!dma_write(evt_addr, evt.data(), SMMUV3_EVENT_SIZE)) {
        set_secure_gerror(GERROR_BIT_EVENTQ_ABT);
        return;
    }

    prod = (prod + 1) & ((1u << (log2size + 1)) - 1);
    S_EVENTQ_PROD = prod;
    update_secure_irq_levels();
}

template <unsigned int BUSWIDTH>
bool smmuv3<BUSWIDTH>::queue_full(uint32_t prod, uint32_t cons, uint32_t size)
{
    size = clamp_queue_log2size(size);
    uint32_t mask = (1u << size) - 1;
    uint32_t wrap = 1u << size;
    return ((prod ^ cons) == wrap) && ((prod & mask) == (cons & mask));
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::record_event(uint32_t type, uint32_t sid, uint64_t iova, uint32_t info)
{
    if (static_cast<uint32_t>(CR0[CR0_EVENTQEN]) == 0) {
        return;
    }

    constexpr uint64_t QUEUE_BASE_LOG2_BITS = 5;
    constexpr size_t EVT_SID_OFFSET = 4;
    constexpr size_t EVT_IOVA_OFFSET = 8;
    constexpr size_t EVT_INFO_OFFSET = 16;

    uint64_t base = (static_cast<uint64_t>(EVENTQ_BASE_HI) << 32) |
                    (static_cast<uint64_t>(EVENTQ_BASE_LO) & ~((1ULL << QUEUE_BASE_LOG2_BITS) - 1));
    uint32_t log2size = clamp_queue_log2size(static_cast<uint32_t>(EVENTQ_BASE_LO[EVENTQ_BASE_LOG2SIZE]));
    uint32_t prod = static_cast<uint32_t>(EVENTQ_PROD);
    uint32_t cons = static_cast<uint32_t>(EVENTQ_CONS);

    if (queue_full(prod, cons, log2size)) {
        set_gerror(GERROR_BIT_EVENTQ_ABT);
        return;
    }

    uint32_t idx = prod & ((1u << log2size) - 1);
    uint64_t evt_addr = base + (idx * SMMUV3_EVENT_SIZE);
    std::array<uint8_t, SMMUV3_EVENT_SIZE> evt{};

    evt[0] = type;
    store_le<uint32_t>(evt.data() + EVT_SID_OFFSET, sid);
    store_le<uint64_t>(evt.data() + EVT_IOVA_OFFSET, iova);
    store_le<uint32_t>(evt.data() + EVT_INFO_OFFSET, info);

    if (!dma_write(evt_addr, evt.data(), SMMUV3_EVENT_SIZE)) {
        set_gerror(GERROR_BIT_EVENTQ_ABT);
        return;
    }

    prod = (prod + 1) & ((1u << (log2size + 1)) - 1);
    EVENTQ_PROD = prod;
    update_irq_levels();
}

template <unsigned int BUSWIDTH>
bool smmuv3<BUSWIDTH>::stall_and_wait(uint32_t sid, uint32_t ssid, uint64_t iova, uint32_t fault_type,
                                      uint32_t fault_level, uint32_t class_, tlm::tlm_generic_payload& txn)
{
    uint32_t stag = m_next_stag++;
    auto& stalled = m_stalled_txns[stag];
    stalled.stag = stag;
    stalled.sid = sid;
    stalled.substream_id = ssid;
    stalled.aborted = false;

    record_event_full(fault_type, sid, ssid, iova, txn, class_, fault_level, stag, true);

    sc_core::wait(stalled.resume_event);

    bool aborted = stalled.aborted;
    m_stalled_txns.erase(stag);
    return aborted;
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::record_event_full(uint32_t type, uint32_t sid, uint32_t ssid, uint64_t iova,
                                         tlm::tlm_generic_payload& txn, uint32_t class_, uint32_t fault_level,
                                         uint32_t stag, bool stall)
{
    if (static_cast<uint32_t>(CR0[CR0_EVENTQEN]) == 0) return;

    constexpr uint64_t QUEUE_BASE_LOG2_BITS = 5;
    uint64_t base = (static_cast<uint64_t>(EVENTQ_BASE_HI) << 32) |
                    (static_cast<uint64_t>(EVENTQ_BASE_LO) & ~((1ULL << QUEUE_BASE_LOG2_BITS) - 1));
    uint32_t log2size = clamp_queue_log2size(static_cast<uint32_t>(EVENTQ_BASE_LO[EVENTQ_BASE_LOG2SIZE]));
    uint32_t prod = static_cast<uint32_t>(EVENTQ_PROD);
    uint32_t cons = static_cast<uint32_t>(EVENTQ_CONS);

    if (queue_full(prod, cons, log2size)) {
        set_gerror(GERROR_BIT_EVENTQ_ABT);
        return;
    }

    uint32_t idx = prod & ((1u << log2size) - 1);
    uint64_t evt_addr = base + (idx * SMMUV3_EVENT_SIZE);
    std::array<uint64_t, 4> evt{};

    bool rnw = (txn.get_command() == tlm::TLM_READ_COMMAND);
    bool ssv = (ssid != 0);

    evt[0] = (static_cast<uint64_t>(type) & EVT_W0_TYPE_MASK) | (ssv ? (1ULL << EVT_W0_SSV_BIT) : 0ULL) |
             ((static_cast<uint64_t>(ssid) & EVT_W0_SSID_MASK) << EVT_W0_SSID_SHIFT) |
             (static_cast<uint64_t>(sid) << EVT_W0_STREAMID_SHIFT);

    evt[1] = (static_cast<uint64_t>(stag) & EVT_W1_STAG_MASK) | (rnw ? (1ULL << EVT_W1_RNW_BIT) : 0ULL) |
             ((static_cast<uint64_t>(class_) & EVT_W1_CLASS_MASK) << EVT_W1_CLASS_SHIFT) |
             (stall ? (1ULL << EVT_W1_STALL_BIT) : 0ULL) |
             ((static_cast<uint64_t>(fault_level) & EVT_W1_REASON_MASK) << EVT_W1_REASON_SHIFT);

    evt[2] = iova;
    evt[3] = 0;

    if (!dma_write(evt_addr, evt.data(), SMMUV3_EVENT_SIZE)) {
        set_gerror(GERROR_BIT_EVENTQ_ABT);
        return;
    }

    prod = (prod + 1) & ((1u << (log2size + 1)) - 1);
    EVENTQ_PROD = prod;
    update_irq_levels();
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::record_pri(uint32_t sid, uint64_t iova, uint32_t flags)
{
    if (static_cast<uint32_t>(CR0[CR0_PRIQEN]) == 0) {
        return;
    }

    constexpr uint64_t QUEUE_BASE_LOG2_BITS = 5;
    constexpr size_t PRI_SID_OFFSET = 0;
    constexpr size_t PRI_FLAGS_OFFSET = 4;
    constexpr size_t PRI_IOVA_OFFSET = 8;

    uint64_t base = (static_cast<uint64_t>(PRIQ_BASE_HI) << 32) |
                    (static_cast<uint64_t>(PRIQ_BASE_LO) & ~((1ULL << QUEUE_BASE_LOG2_BITS) - 1));
    uint32_t log2size = clamp_queue_log2size(static_cast<uint32_t>(PRIQ_BASE_LO[PRIQ_BASE_LOG2SIZE]));
    uint32_t prod = static_cast<uint32_t>(PRIQ_PROD);
    uint32_t cons = static_cast<uint32_t>(PRIQ_CONS);

    if (queue_full(prod, cons, log2size)) {
        set_gerror(GERROR_BIT_PRIQ_ABT);
        return;
    }

    uint32_t idx = prod & ((1u << log2size) - 1);
    uint64_t pri_addr = base + (idx * SMMUV3_PRI_SIZE);
    std::array<uint8_t, SMMUV3_PRI_SIZE> pri{};

    store_le<uint32_t>(pri.data() + PRI_SID_OFFSET, sid);
    store_le<uint32_t>(pri.data() + PRI_FLAGS_OFFSET, flags);
    store_le<uint64_t>(pri.data() + PRI_IOVA_OFFSET, iova);

    if (!dma_write(pri_addr, pri.data(), SMMUV3_PRI_SIZE)) {
        set_gerror(GERROR_BIT_PRIQ_ABT);
        return;
    }

    prod = (prod + 1) & ((1u << (log2size + 1)) - 1);
    PRIQ_PROD = prod;
    update_irq_levels();
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::handle_cmd_prefetch_config(const uint8_t*)
{
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::handle_cmd_prefetch_addr(const uint8_t*)
{
}

namespace detail_smmuv3 {
constexpr size_t CMD_SID_OFFSET = 4;
constexpr size_t CMD_ASID_OFFSET = 6;
constexpr size_t CMD_WORD1_OFFSET = 8;
constexpr size_t CMD_STAG_OFFSET = 4;
constexpr size_t CMD_RESUME_RESP_OFFSET = 11;
constexpr uint32_t CMD_WORD0_TG_SHIFT = 10;
constexpr uint32_t CMD_WORD0_TG_MASK = 0x3;
constexpr uint32_t CMD_WORD0_SCALE_SHIFT = 20;
constexpr uint32_t CMD_WORD0_SCALE_MASK = 0xF;
constexpr uint32_t CMD_WORD0_VMID_SHIFT = 32;
constexpr uint32_t CMD_WORD0_VMID_MASK = 0xFFFF;
constexpr uint32_t CMD_WORD0_ASID_SHIFT = 48;
constexpr uint32_t CMD_WORD0_ASID_MASK = 0xFFFF;
constexpr uint32_t CMD_SYNC_CS_SHIFT = 12;
constexpr uint32_t CMD_SYNC_CS_MASK = 0x3;
constexpr uint64_t CMD_VA_LOW_MASK = 0xFFFULL;

struct TlbiTgDecode {
    uint32_t page_shift;
    uint8_t key_tg;
};

inline TlbiTgDecode decode_tlbi_tg(uint8_t tlbi_tg)
{
    switch (tlbi_tg) {
    case TLBI_TG_4K:
        return { PAGE_SHIFT_4K, CD_TG_4K };
    case TLBI_TG_16K:
        return { PAGE_SHIFT_16K, CD_TG_16K };
    case TLBI_TG_64K:
        return { PAGE_SHIFT_64K, CD_TG_64K };
    default:
        return { PAGE_SHIFT_4K, CD_TG_4K };
    }
}
} // namespace detail_smmuv3

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::handle_cmd_cfgi_ste(const uint8_t* cmd)
{
    uint32_t sid = load_le<uint32_t>(cmd + detail_smmuv3::CMD_SID_OFFSET);
    m_ste_cache.erase(sid);
    m_ste_cache_lru.remove(sid);
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::handle_cmd_cfgi_ste_range(const uint8_t*)
{
    m_ste_cache.clear();
    m_ste_cache_lru.clear();
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::handle_cmd_cfgi_cd(const uint8_t*)
{
    m_cd_cache.clear();
    m_cd_cache_lru.clear();
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::handle_cmd_cfgi_cd_all(const uint8_t*)
{
    m_cd_cache.clear();
    m_cd_cache_lru.clear();
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::handle_cmd_cfgi_all(const uint8_t*)
{
    m_ste_cache.clear();
    m_ste_cache_lru.clear();
    m_cd_cache.clear();
    m_cd_cache_lru.clear();
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::handle_cmd_tlbi_nh_all(const uint8_t*)
{
    iotlb_inv_all();
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::handle_cmd_tlbi_nh_asid(const uint8_t* cmd)
{
    uint16_t asid = load_le<uint16_t>(cmd + detail_smmuv3::CMD_ASID_OFFSET);
    iotlb_inv_asid(asid);
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::handle_cmd_tlbi_nh_va(const uint8_t* cmd)
{
    uint64_t word0 = load_le<uint64_t>(cmd);
    uint64_t word1 = load_le<uint64_t>(cmd + detail_smmuv3::CMD_WORD1_OFFSET);
    uint16_t asid = (word0 >> detail_smmuv3::CMD_WORD0_ASID_SHIFT) & detail_smmuv3::CMD_WORD0_ASID_MASK;
    uint8_t tlbi_tg = (word0 >> detail_smmuv3::CMD_WORD0_TG_SHIFT) & detail_smmuv3::CMD_WORD0_TG_MASK;
    uint8_t scale = (word0 >> detail_smmuv3::CMD_WORD0_SCALE_SHIFT) & detail_smmuv3::CMD_WORD0_SCALE_MASK;
    uint64_t va = word1 & ~detail_smmuv3::CMD_VA_LOW_MASK;

    detail_smmuv3::TlbiTgDecode tg = detail_smmuv3::decode_tlbi_tg(tlbi_tg);
    uint64_t mask = safe_shl1_u64(tg.page_shift + scale) - 1;
    iotlb_inv_iova(0, asid, va, tg.key_tg, mask);
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::handle_cmd_tlbi_nh_vaa(const uint8_t* cmd)
{
    uint64_t word0 = load_le<uint64_t>(cmd);
    uint64_t word1 = load_le<uint64_t>(cmd + detail_smmuv3::CMD_WORD1_OFFSET);
    uint8_t tlbi_tg = (word0 >> detail_smmuv3::CMD_WORD0_TG_SHIFT) & detail_smmuv3::CMD_WORD0_TG_MASK;
    uint8_t scale = (word0 >> detail_smmuv3::CMD_WORD0_SCALE_SHIFT) & detail_smmuv3::CMD_WORD0_SCALE_MASK;
    uint64_t va = word1 & ~detail_smmuv3::CMD_VA_LOW_MASK;

    detail_smmuv3::TlbiTgDecode tg = detail_smmuv3::decode_tlbi_tg(tlbi_tg);
    uint64_t mask = safe_shl1_u64(tg.page_shift + scale) - 1;
    uint64_t va_masked = va & ~mask;
    uint8_t key_tg = tg.key_tg;

    for (auto it = m_iotlb.begin(); it != m_iotlb.end();) {
        uint64_t entry_masked = it->first.iova & ~mask;
        if (entry_masked == va_masked && it->first.tg == key_tg) {
            m_iotlb_lru.remove(it->first);
            it = m_iotlb.erase(it);
        } else {
            ++it;
        }
    }
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::handle_cmd_atc_inv(const uint8_t* cmd)
{
    constexpr uint32_t CMD_WORD0_SID_SHIFT = 32;
    constexpr uint64_t CMD_ATC_SIZE_MASK = 0x3FULL;
    constexpr uint64_t CMD_ATC_ADDR_MASK = ~((1ULL << PAGE_SHIFT_4K) - 1ULL);

    uint64_t word0 = load_le<uint64_t>(cmd);
    uint64_t word1 = load_le<uint64_t>(cmd + detail_smmuv3::CMD_WORD1_OFFSET);
    uint32_t sid = static_cast<uint32_t>(word0 >> CMD_WORD0_SID_SHIFT);
    uint8_t size = word1 & CMD_ATC_SIZE_MASK;
    uint64_t addr_lo = word1 & CMD_ATC_ADDR_MASK;

    uint64_t range = safe_shl1_u64(size + PAGE_SHIFT_4K);
    uint64_t end = addr_lo + range - 1;

    SCP_INFO(()) << "SMMUv3: ATC_INV sid=0x" << std::hex << sid << " addr=0x" << addr_lo << " range=0x" << range;

    for (auto* tbu : tbus) {
        if (!tbu) continue;
        tbu->upstream_invalidate(addr_lo, end);
    }
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::handle_cmd_tlbi_s12_vmall(const uint8_t* cmd)
{
    uint64_t word0 = load_le<uint64_t>(cmd);
    uint16_t vmid = (word0 >> detail_smmuv3::CMD_WORD0_VMID_SHIFT) & detail_smmuv3::CMD_WORD0_VMID_MASK;
    iotlb_inv_vmid(vmid);
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::handle_cmd_tlbi_s2_ipa(const uint8_t* cmd)
{
    uint64_t word0 = load_le<uint64_t>(cmd);
    uint64_t word1 = load_le<uint64_t>(cmd + detail_smmuv3::CMD_WORD1_OFFSET);
    uint16_t vmid = (word0 >> detail_smmuv3::CMD_WORD0_VMID_SHIFT) & detail_smmuv3::CMD_WORD0_VMID_MASK;
    uint8_t tlbi_tg = (word0 >> detail_smmuv3::CMD_WORD0_TG_SHIFT) & detail_smmuv3::CMD_WORD0_TG_MASK;
    uint8_t scale = (word0 >> detail_smmuv3::CMD_WORD0_SCALE_SHIFT) & detail_smmuv3::CMD_WORD0_SCALE_MASK;
    uint64_t ipa = word1 & IPA_ADDR_MASK;

    detail_smmuv3::TlbiTgDecode tg = detail_smmuv3::decode_tlbi_tg(tlbi_tg);
    uint64_t mask = safe_shl1_u64(tg.page_shift + scale) - 1;
    uint64_t ipa_masked = ipa & ~mask;
    uint8_t key_tg = tg.key_tg;

    for (auto it = m_iotlb.begin(); it != m_iotlb.end();) {
        uint64_t entry_masked = it->first.iova & ~mask;
        if (it->first.vmid == vmid && entry_masked == ipa_masked && it->first.tg == key_tg) {
            m_iotlb_lru.remove(it->first);
            it = m_iotlb.erase(it);
        } else {
            ++it;
        }
    }
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::handle_cmd_tlbi_nsnh_all(const uint8_t*)
{
    iotlb_inv_all();
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::handle_cmd_sync(const uint8_t* cmd)
{
    uint64_t dw0 = load_le<uint64_t>(cmd);
    uint64_t dw1 = load_le<uint64_t>(cmd + detail_smmuv3::CMD_WORD1_OFFSET);

    uint32_t cs = (static_cast<uint32_t>(dw0) >> detail_smmuv3::CMD_SYNC_CS_SHIFT) & detail_smmuv3::CMD_SYNC_CS_MASK;

    if (cs == SYNC_CS_IRQ) {
        constexpr uint64_t CMD_SYNC_MSIADDR_MASK = 0x000FFFFFFFFFFFFCULL;
        constexpr uint32_t CMD_SYNC_MSIDATA_SHIFT = 32;
        uint64_t msiaddr = dw1 & CMD_SYNC_MSIADDR_MASK;
        uint32_t msidata = static_cast<uint32_t>(dw0 >> CMD_SYNC_MSIDATA_SHIFT);
        if (msiaddr != 0) {
            if (!dma_write(msiaddr, &msidata, sizeof(msidata))) {
                set_gerror(GERROR_BIT_MSI_CMDQ_ABT);
            }
        }
        trigger_irq(irq_cmd_sync);
    }
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::handle_cmd_resume(const uint8_t* cmd)
{
    uint32_t stag = load_le<uint32_t>(cmd + detail_smmuv3::CMD_STAG_OFFSET);
    uint8_t resp = cmd[detail_smmuv3::CMD_RESUME_RESP_OFFSET];

    auto it = m_stalled_txns.find(stag);
    if (it != m_stalled_txns.end()) {
        if (resp != 0) {
            it->second.aborted = true;
        }
        it->second.resume_event.notify();
    }
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::handle_cmd_pri_resp(const uint8_t*)
{
}

template <unsigned int BUSWIDTH>
void smmuv3<BUSWIDTH>::do_gatos()
{
    GATOS_CTRL[GATOS_CTRL_INPRG] = 1;

    uint32_t sid = static_cast<uint32_t>(GATOS_SID);
    uint64_t iova = (static_cast<uint64_t>(GATOS_ADDR_HI) << 32) | static_cast<uint64_t>(GATOS_ADDR_LO);
    bool wr = static_cast<uint32_t>(GATOS_CTRL[GATOS_CTRL_READ_NWRITE]) == 0;

    tlm::tlm_generic_payload dummy_txn;
    dummy_txn.set_command(wr ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
    dummy_txn.set_address(iova);

    IOMMUTLBEntry te = smmuv3_translate(dummy_txn, sid, 0);

    if (te.perm == IOMMUAccessFlags::NONE) {
        GATOS_PAR_LO[GATOS_PAR_F] = 1;
        GATOS_PAR_LO[GATOS_PAR_FST] = GATOS_PAR_FAULT_TRANSLATION;
    } else {
        GATOS_PAR_LO = static_cast<uint32_t>(te.translated_addr);
        GATOS_PAR_HI = static_cast<uint32_t>(te.translated_addr >> 32);
        GATOS_PAR_LO[GATOS_PAR_F] = 0;
    }

    GATOS_CTRL[GATOS_CTRL_RUN] = 0;
    GATOS_CTRL[GATOS_CTRL_INPRG] = 0;
}

} // namespace gs

#endif
