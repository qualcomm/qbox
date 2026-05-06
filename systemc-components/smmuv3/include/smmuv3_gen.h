/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef SMMUV3_GEN_H
#define SMMUV3_GEN_H

#include <registers.h>
#include <reg_model_maker/reg_model_maker.h>
#include <string>
#include <vector>
#include <memory>

namespace gs {

// Backing store for all SMMU MMIO registers. "gs_register" is one 32-bit register and
// "gs_field" is a named bit-slice within a register. Acronyms are explained the first time they appear.
class smmuv3_gen
{
public:
    // IDR0..IDR5 = ID Registers: read-only feature/capability bits the driver inspects at boot.
    gs_register<uint32_t> IDR0;
    gs_field<uint32_t> IDR0_S2P;    // S2P = Stage-2 translation Present (i.e. supported)
    gs_field<uint32_t> IDR0_S1P;    // S1P = Stage-1 translation Present
    gs_field<uint32_t> IDR0_TTF;    // TTF = Translation Table Format support (AArch32/AArch64)
    gs_field<uint32_t> IDR0_COHACC; // COHACC = Coherent ACCess: SMMU's own page-walks are coherent with CPU caches
    gs_field<uint32_t> IDR0_ASID16; // ASID16 = ASID (Address-Space ID) is 16 bits wide (vs 8)
    gs_field<uint32_t> IDR0_VMID16; // VMID16 = VMID (Virtual-Machine ID) is 16 bits wide (vs 8)
    gs_field<uint32_t> IDR0_PRI;    // PRI = Page Request Interface (PCIe-style page-fault delivery) supported
    gs_field<uint32_t>
        IDR0_ATOS; // ATOS = Address Translation Operations Support (debug-only translate-this-VA channel)
    gs_field<uint32_t> IDR0_HTTU;    // HTTU = Hardware Translation Table Update (HW updates Access/Dirty bits)
    gs_field<uint32_t> IDR0_STLEVEL; // STLEVEL = Stream Table LEVEL: 1-level (linear) vs 2-level table support
    gs_field<uint32_t> IDR0_MSI; // MSI = Message Signalled Interrupts supported (writes-to-address instead of wires)
    gs_field<uint32_t> IDR0_ATS; // ATS = Address Translation Service (PCIe ATS) supported

    gs_register<uint32_t> IDR1;
    gs_field<uint32_t> IDR1_SIDSIZE; // SIDSIZE = number of bits in a StreamID (selects the device on the upstream side)
    gs_field<uint32_t> IDR1_EVENTQS; // EVENTQS = Event Queue Size (log2 of max entries)
    gs_field<uint32_t> IDR1_CMDQS;   // CMDQS = Command Queue Size (log2 of max entries)

    gs_register<uint32_t> IDR2;
    gs_register<uint32_t> IDR3;
    gs_field<uint32_t> IDR3_BBML1; // BBML1 = Break-Before-Make Level 1: HW supports level-1 BBM mapping changes
    gs_field<uint32_t> IDR3_BBML2; // BBML2 = Break-Before-Make Level 2: stronger guarantees on live-mapping changes

    gs_register<uint32_t> IDR4;
    gs_register<uint32_t> IDR5;
    gs_field<uint32_t> IDR5_OAS;     // OAS = Output Address Size: how wide the resulting physical address can be
    gs_field<uint32_t> IDR5_GRAN4K;  // GRAN4K = 4 KiB page granule supported
    gs_field<uint32_t> IDR5_GRAN16K; // GRAN16K = 16 KiB page granule supported
    gs_field<uint32_t> IDR5_GRAN64K; // GRAN64K = 64 KiB page granule supported

    gs_register<uint32_t> IIDR; // IIDR = Implementation ID Register (vendor/product/revision)
    gs_register<uint32_t> AIDR; // AIDR = Architecture ID Register (which arch revision this implements)

    // CR = Control Register: writeable knobs the driver uses to configure the SMMU at runtime.
    gs_register<uint32_t> CR0;
    gs_field<uint32_t> CR0_SMMUEN;   // SMMUEN = SMMU Enable: turn the whole translator on/off
    gs_field<uint32_t> CR0_PRIQEN;   // PRIQEN = Page Request Queue Enable
    gs_field<uint32_t> CR0_EVENTQEN; // EVENTQEN = Event Queue Enable
    gs_field<uint32_t> CR0_CMDQEN;   // CMDQEN = Command Queue Enable
    gs_field<uint32_t> CR0_ATSCHK; // ATSCHK = ATS Check: validate that incoming pre-translated transactions are allowed
    gs_field<uint32_t> CR0_NSCFG;  // NSCFG = Non-Secure CFG override (controls how the NS attribute is forced)
    gs_field<uint32_t> CR0_VMW;    // VMW = VMID Wildcard match (for stage-2 invalidations)

    gs_register<uint32_t>
        CR0ACK; // ACK = Acknowledge: hardware mirror of CR0 the driver polls to confirm a write took effect
    gs_register<uint32_t> CR1;
    // For each of the SMMU's own memory accesses (page-table walks, queue accesses), CR1 fixes
    // the cache/shareability hints. SH = SHareability, OC = Outer Cacheability, IC = Inner Cacheability.
    gs_field<uint32_t> CR1_TABLE_SH;
    gs_field<uint32_t> CR1_TABLE_OC;
    gs_field<uint32_t> CR1_TABLE_IC;
    gs_field<uint32_t> CR1_QUEUE_SH;
    gs_field<uint32_t> CR1_QUEUE_OC;
    gs_field<uint32_t> CR1_QUEUE_IC;

    gs_register<uint32_t> CR2;
    gs_field<uint32_t>
        CR2_PTM; // PTM = Private TLB Maintenance: invalidations from the CPU are not broadcast to this SMMU
    gs_field<uint32_t>
        CR2_RECINVSID; // RECINVSID = Record INValid SID: log events when a transaction arrives with a bad StreamID
    gs_field<uint32_t> CR2_E2H; // E2H = EL2 Host bit (matches the CPU-side hypervisor-host extension semantics)

    gs_register<uint32_t> STATUSR; // STATUSR = STATUS Register (general status bits, e.g. quiesced)
    gs_register<uint32_t>
        GBPA; // GBPA = Global ByPass Attributes: how to treat traffic when no Stream Table Entry matches
    gs_field<uint32_t> GBPA_UPDATE; // UPDATE = self-clearing bit the driver sets to commit a new GBPA value atomically
    gs_field<uint32_t> GBPA_ABORT;  // ABORT = if set, abort un-matched traffic instead of letting it pass through

    gs_register<uint32_t> AGBPA; // AGBPA = Auxiliary GBPA (vendor/implementation-defined extra bypass knobs)

    gs_register<uint32_t>
        IRQ_CTRL; // IRQ_CTRL = Interrupt Request Control: master enable bits for the three IRQ sources
    gs_field<uint32_t>
        IRQ_CTRL_GERROR_IRQEN;             // IRQEN = IRQ ENable. GERROR = Global ERROR (catastrophic-error interrupt)
    gs_field<uint32_t> IRQ_CTRL_PRI_IRQEN; // PRI = Page Request Interface interrupt enable
    gs_field<uint32_t>
        IRQ_CTRL_EVENTQ_IRQEN; // EVENTQ = Event Queue interrupt enable (faults/events written to memory queue)

    gs_register<uint32_t> IRQ_CTRL_ACK; // Driver-readable mirror of IRQ_CTRL (write-takes-effect handshake)

    // GERROR holds sticky catastrophic-error bits; the driver clears them by writing the matching
    // bit into GERRORN. ABT = ABorT (a bus error happened while the SMMU itself was accessing memory).
    gs_register<uint32_t> GERROR;
    gs_field<uint32_t> GERROR_CMDQ_ERR;           // command queue saw a malformed/unsupported command
    gs_field<uint32_t> GERROR_EVENTQ_ABT_ERR;     // bus abort while writing into the event queue
    gs_field<uint32_t> GERROR_PRIQ_ABT_ERR;       // bus abort while writing into the PRI queue
    gs_field<uint32_t> GERROR_MSI_CMDQ_ABT_ERR;   // bus abort while sending the cmd-queue MSI
    gs_field<uint32_t> GERROR_MSI_EVENTQ_ABT_ERR; // bus abort while sending the event-queue MSI
    gs_field<uint32_t> GERROR_MSI_PRIQ_ABT_ERR;   // bus abort while sending the PRI-queue MSI
    gs_field<uint32_t> GERROR_MSI_GERROR_ABT_ERR; // bus abort while sending the GERROR MSI itself
    gs_field<uint32_t> GERROR_SFM_ERR;            // SFM = Stream Fault Mode: a fatal stream-walk fault was hit

    gs_register<uint32_t> GERRORN; // GERRORN = GERROR Negate: writing a 1 here clears the matching GERROR bit

    // CFG0_LO/HI = address (low and high halves) the SMMU writes the MSI payload to;
    // CFG1 = MSI attributes; CFG2 = MSI data word.
    gs_register<uint32_t> GERROR_IRQ_CFG0_LO;
    gs_register<uint32_t> GERROR_IRQ_CFG0_HI;
    gs_register<uint32_t> GERROR_IRQ_CFG1;
    gs_register<uint32_t> GERROR_IRQ_CFG2;

    // STRTAB = STReam TABle: an in-memory array of Stream Table Entries (STEs); each entry describes
    // one upstream device (looked up by StreamID). LO/HI hold the 64-bit base address.
    gs_register<uint32_t> STRTAB_BASE_LO;
    gs_register<uint32_t> STRTAB_BASE_HI;
    gs_register<uint32_t> STRTAB_BASE_CFG;
    gs_field<uint32_t> STRTAB_BASE_CFG_FMT;   // FMT = ForMaT: 0 = single-level (linear), other = 2-level
    gs_field<uint32_t> STRTAB_BASE_CFG_SPLIT; // SPLIT = how many bits of StreamID select the L1 entry (rest selects L2)
    gs_field<uint32_t> STRTAB_BASE_CFG_LOG2SIZE; // LOG2SIZE = log2 of total number of StreamIDs the table covers

    // CMDQ = CoMmanD Queue: in-memory ring the driver pushes commands into for the SMMU to consume.
    // PROD = PRODucer index (driver-written); CONS = CONSumer index (SMMU-written).
    gs_register<uint32_t> CMDQ_BASE_LO;
    gs_field<uint32_t> CMDQ_BASE_LOG2SIZE; // log2 of queue size in entries (entry = 16 bytes)

    gs_register<uint32_t> CMDQ_BASE_HI;
    gs_register<uint32_t> CMDQ_PROD;
    gs_field<uint32_t> CMDQ_PROD_WRP; // WRP = WRaP bit: toggles each time the index passes the end (so empty != full)
    gs_field<uint32_t> CMDQ_PROD_RD;  // RD = ReaD index portion of PROD (next slot to write)

    gs_register<uint32_t> CMDQ_CONS;
    gs_field<uint32_t> CMDQ_CONS_WRP;
    gs_field<uint32_t> CMDQ_CONS_RD;
    gs_field<uint32_t> CMDQ_CONS_ERR; // ERR = consumer-side error code if a command was rejected

    // EVENTQ = EVENT Queue: SMMU pushes fault/event records here for the driver to consume.
    gs_register<uint32_t> EVENTQ_BASE_LO;
    gs_field<uint32_t> EVENTQ_BASE_LOG2SIZE;

    gs_register<uint32_t> EVENTQ_BASE_HI;
    gs_register<uint32_t> EVENTQ_PROD;
    gs_field<uint32_t> EVENTQ_PROD_WRP;
    gs_field<uint32_t> EVENTQ_PROD_RD;

    gs_register<uint32_t> EVENTQ_CONS;
    gs_field<uint32_t> EVENTQ_CONS_WRP;
    gs_field<uint32_t> EVENTQ_CONS_RD;

    gs_register<uint32_t> EVENTQ_IRQ_CFG0_LO;
    gs_register<uint32_t> EVENTQ_IRQ_CFG0_HI;
    gs_register<uint32_t> EVENTQ_IRQ_CFG1;
    gs_register<uint32_t> EVENTQ_IRQ_CFG2;

    // PRIQ = Page Request Interface Queue: SMMU pushes PCIe page-fault requests here for the OS to handle.
    gs_register<uint32_t> PRIQ_BASE_LO;
    gs_field<uint32_t> PRIQ_BASE_LOG2SIZE;

    gs_register<uint32_t> PRIQ_BASE_HI;
    gs_register<uint32_t> PRIQ_PROD;
    gs_field<uint32_t> PRIQ_PROD_WRP;
    gs_field<uint32_t> PRIQ_PROD_RD;

    gs_register<uint32_t> PRIQ_CONS;
    gs_field<uint32_t> PRIQ_CONS_WRP;
    gs_field<uint32_t> PRIQ_CONS_RD;

    gs_register<uint32_t> PRIQ_IRQ_CFG0_LO;
    gs_register<uint32_t> PRIQ_IRQ_CFG0_HI;
    gs_register<uint32_t> PRIQ_IRQ_CFG1;
    gs_register<uint32_t> PRIQ_IRQ_CFG2;

    // GATOS = Global Address Translation Operations Support: a debug-only "translate this VA for me"
    // channel. The driver writes a StreamID + VA, kicks RUN, and reads the resulting PA back from PAR.
    gs_register<uint32_t> GATOS_CTRL;
    gs_field<uint32_t> GATOS_CTRL_RUN;         // RUN = self-clearing trigger bit; set by driver to start a translation
    gs_field<uint32_t> GATOS_CTRL_INPRG;       // INPRG = IN-PRoGress: SMMU sets this while the translation is happening
    gs_field<uint32_t> GATOS_CTRL_READ_NWRITE; // READ_NWRITE = 1 means simulate a read access, 0 means a write access

    gs_register<uint32_t> GATOS_SID;     // SID = StreamID of the requester to impersonate
    gs_register<uint32_t> GATOS_ADDR_LO; // input VA, low half
    gs_register<uint32_t> GATOS_ADDR_HI; // input VA, high half
    gs_register<uint32_t> GATOS_PAR_LO;  // PAR = Physical Address Result (translation output), low half
    gs_field<uint32_t> GATOS_PAR_F;   // F = Fault: 1 if translation failed (then PAR holds fault info, not an address)
    gs_field<uint32_t> GATOS_PAR_FST; // FST = Fault STatus: encoded reason code when F = 1

    gs_register<uint32_t> GATOS_PAR_HI; // PAR high half

    // IDREGS = block of read-only "PrimeCell" identification registers at the top of the page (vendor/part/revision
    // IDs)
    std::vector<std::shared_ptr<gs_register<uint32_t>>> IDREGS;

    // _P1 = Page 1 alias: SMMUv3 exposes a second 64 KiB page that mirrors a few queue pointers,
    // letting the driver reach them through a separate VMM/EL2 mapping without exposing the whole register file.
    gs_register<uint32_t> CMDQ_PROD_P1;
    gs_register<uint32_t> CMDQ_CONS_P1;
    gs_register<uint32_t> EVENTQ_PROD_P1;
    gs_register<uint32_t> EVENTQ_CONS_P1;
    gs_register<uint32_t> PRIQ_PROD_P1;
    gs_register<uint32_t> PRIQ_CONS_P1;

    // S_ prefix = Secure: a parallel set of registers, only accessible from Secure-world software,
    // controlling translations for Secure-tagged transactions. Field meanings mirror their non-S_ twins.
    gs_register<uint32_t> S_IDR0;
    gs_register<uint32_t> S_IDR1;
    gs_register<uint32_t> S_IDR2;
    gs_register<uint32_t> S_IDR3;
    gs_register<uint32_t> S_IDR4;

    gs_register<uint32_t> S_CR0;
    gs_field<uint32_t> S_CR0_SMMUEN;
    gs_field<uint32_t> S_CR0_EVENTQEN;
    gs_field<uint32_t> S_CR0_CMDQEN;
    gs_register<uint32_t> S_CR0ACK;
    gs_register<uint32_t> S_CR1;
    gs_register<uint32_t> S_CR2;
    gs_register<uint32_t> S_INIT; // INIT = INITialise: secure-only register the secure firmware uses to reset state
    gs_field<uint32_t> S_INIT_INV_ALL; // INV_ALL = INValidate ALL caches/TLBs (self-clearing trigger)
    gs_register<uint32_t> S_GBPA;
    gs_field<uint32_t> S_GBPA_UPDATE;
    gs_field<uint32_t> S_GBPA_ABORT;
    gs_register<uint32_t> S_AGBPA;

    gs_register<uint32_t> S_IRQ_CTRL;
    gs_field<uint32_t> S_IRQ_CTRL_GERROR_IRQEN;
    gs_field<uint32_t> S_IRQ_CTRL_EVENTQ_IRQEN;
    gs_register<uint32_t> S_IRQ_CTRL_ACK;

    gs_register<uint32_t> S_GERROR;
    gs_field<uint32_t> S_GERROR_CMDQ_ERR;
    gs_field<uint32_t> S_GERROR_EVENTQ_ABT_ERR;
    gs_field<uint32_t> S_GERROR_MSI_CMDQ_ABT_ERR;
    gs_field<uint32_t> S_GERROR_MSI_EVENTQ_ABT_ERR;
    gs_field<uint32_t> S_GERROR_MSI_GERROR_ABT_ERR;
    gs_field<uint32_t> S_GERROR_SFM_ERR;
    gs_register<uint32_t> S_GERRORN;

    gs_register<uint32_t> S_GERROR_IRQ_CFG0_LO;
    gs_register<uint32_t> S_GERROR_IRQ_CFG0_HI;
    gs_register<uint32_t> S_GERROR_IRQ_CFG1;
    gs_register<uint32_t> S_GERROR_IRQ_CFG2;

    gs_register<uint32_t> S_STRTAB_BASE_LO;
    gs_register<uint32_t> S_STRTAB_BASE_HI;
    gs_register<uint32_t> S_STRTAB_BASE_CFG;
    gs_field<uint32_t> S_STRTAB_BASE_CFG_FMT;
    gs_field<uint32_t> S_STRTAB_BASE_CFG_SPLIT;
    gs_field<uint32_t> S_STRTAB_BASE_CFG_LOG2SIZE;

    gs_register<uint32_t> S_CMDQ_BASE_LO;
    gs_field<uint32_t> S_CMDQ_BASE_LOG2SIZE;
    gs_register<uint32_t> S_CMDQ_BASE_HI;
    gs_register<uint32_t> S_CMDQ_PROD;
    gs_field<uint32_t> S_CMDQ_PROD_WRP;
    gs_field<uint32_t> S_CMDQ_PROD_RD;
    gs_register<uint32_t> S_CMDQ_CONS;
    gs_field<uint32_t> S_CMDQ_CONS_WRP;
    gs_field<uint32_t> S_CMDQ_CONS_RD;
    gs_field<uint32_t> S_CMDQ_CONS_ERR;

    gs_register<uint32_t> S_EVENTQ_BASE_LO;
    gs_field<uint32_t> S_EVENTQ_BASE_LOG2SIZE;
    gs_register<uint32_t> S_EVENTQ_BASE_HI;
    gs_register<uint32_t> S_EVENTQ_PROD;
    gs_field<uint32_t> S_EVENTQ_PROD_WRP;
    gs_field<uint32_t> S_EVENTQ_PROD_RD;
    gs_register<uint32_t> S_EVENTQ_CONS;
    gs_field<uint32_t> S_EVENTQ_CONS_WRP;
    gs_field<uint32_t> S_EVENTQ_CONS_RD;

    gs_register<uint32_t> S_EVENTQ_IRQ_CFG0_LO;
    gs_register<uint32_t> S_EVENTQ_IRQ_CFG0_HI;
    gs_register<uint32_t> S_EVENTQ_IRQ_CFG1;
    gs_register<uint32_t> S_EVENTQ_IRQ_CFG2;

    smmuv3_gen()
        : IDR0("IDR0", "smmuv3.IDR0", 0x0, 1)
        , IDR0_S2P(IDR0, "smmuv3.IDR0.S2P", 0, 1)
        , IDR0_S1P(IDR0, "smmuv3.IDR0.S1P", 1, 1)
        , IDR0_TTF(IDR0, "smmuv3.IDR0.TTF", 2, 2)
        , IDR0_COHACC(IDR0, "smmuv3.IDR0.COHACC", 4, 1)
        , IDR0_ASID16(IDR0, "smmuv3.IDR0.ASID16", 12, 1)
        , IDR0_VMID16(IDR0, "smmuv3.IDR0.VMID16", 18, 1)
        , IDR0_PRI(IDR0, "smmuv3.IDR0.PRI", 16, 1)
        , IDR0_ATOS(IDR0, "smmuv3.IDR0.ATOS", 15, 1)
        , IDR0_HTTU(IDR0, "smmuv3.IDR0.HTTU", 6, 2)
        , IDR0_STLEVEL(IDR0, "smmuv3.IDR0.STLEVEL", 27, 2)
        , IDR0_MSI(IDR0, "smmuv3.IDR0.MSI", 13, 1)
        , IDR0_ATS(IDR0, "smmuv3.IDR0.ATS", 10, 1)
        , IDR1("IDR1", "smmuv3.IDR1", 0x4, 1)
        , IDR1_SIDSIZE(IDR1, "smmuv3.IDR1.SIDSIZE", 0, 6)
        , IDR1_EVENTQS(IDR1, "smmuv3.IDR1.EVENTQS", 16, 5)
        , IDR1_CMDQS(IDR1, "smmuv3.IDR1.CMDQS", 21, 5)
        , IDR2("IDR2", "smmuv3.IDR2", 0x8, 1)
        , IDR3("IDR3", "smmuv3.IDR3", 0xC, 1)
        , IDR3_BBML1(IDR3, "smmuv3.IDR3.BBML1", 11, 1)
        , IDR3_BBML2(IDR3, "smmuv3.IDR3.BBML2", 12, 1)
        , IDR4("IDR4", "smmuv3.IDR4", 0x10, 1)
        , IDR5("IDR5", "smmuv3.IDR5", 0x14, 1)
        , IDR5_OAS(IDR5, "smmuv3.IDR5.OAS", 0, 3)
        , IDR5_GRAN4K(IDR5, "smmuv3.IDR5.GRAN4K", 4, 1)
        , IDR5_GRAN16K(IDR5, "smmuv3.IDR5.GRAN16K", 5, 1)
        , IDR5_GRAN64K(IDR5, "smmuv3.IDR5.GRAN64K", 6, 1)
        , IIDR("IIDR", "smmuv3.IIDR", 0x18, 1)
        , AIDR("AIDR", "smmuv3.AIDR", 0x1C, 1)
        , CR0("CR0", "smmuv3.CR0", 0x20, 1)
        , CR0_SMMUEN(CR0, "smmuv3.CR0.SMMUEN", 0, 1)
        , CR0_PRIQEN(CR0, "smmuv3.CR0.PRIQEN", 1, 1)
        , CR0_EVENTQEN(CR0, "smmuv3.CR0.EVENTQEN", 2, 1)
        , CR0_CMDQEN(CR0, "smmuv3.CR0.CMDQEN", 3, 1)
        , CR0_ATSCHK(CR0, "smmuv3.CR0.ATSCHK", 4, 1)
        , CR0_VMW(CR0, "smmuv3.CR0.VMW", 6, 3)
        , CR0_NSCFG(CR0, "smmuv3.CR0.NSCFG", 28, 2)
        , CR0ACK("CR0ACK", "smmuv3.CR0ACK", 0x24, 1)
        , CR1("CR1", "smmuv3.CR1", 0x28, 1)
        , CR1_TABLE_SH(CR1, "smmuv3.CR1.TABLE_SH", 10, 2)
        , CR1_TABLE_OC(CR1, "smmuv3.CR1.TABLE_OC", 8, 2)
        , CR1_TABLE_IC(CR1, "smmuv3.CR1.TABLE_IC", 6, 2)
        , CR1_QUEUE_SH(CR1, "smmuv3.CR1.QUEUE_SH", 4, 2)
        , CR1_QUEUE_OC(CR1, "smmuv3.CR1.QUEUE_OC", 2, 2)
        , CR1_QUEUE_IC(CR1, "smmuv3.CR1.QUEUE_IC", 0, 2)
        , CR2("CR2", "smmuv3.CR2", 0x2C, 1)
        , CR2_PTM(CR2, "smmuv3.CR2.PTM", 2, 1)
        , CR2_RECINVSID(CR2, "smmuv3.CR2.RECINVSID", 1, 1)
        , CR2_E2H(CR2, "smmuv3.CR2.E2H", 0, 1)
        , STATUSR("STATUSR", "smmuv3.STATUSR", 0x40, 1)
        , GBPA("GBPA", "smmuv3.GBPA", 0x44, 1)
        , GBPA_UPDATE(GBPA, "smmuv3.GBPA.UPDATE", 31, 1)
        , GBPA_ABORT(GBPA, "smmuv3.GBPA.ABORT", 20, 1)
        , AGBPA("AGBPA", "smmuv3.AGBPA", 0x48, 1)
        , IRQ_CTRL("IRQ_CTRL", "smmuv3.IRQ_CTRL", 0x50, 1)
        , IRQ_CTRL_GERROR_IRQEN(IRQ_CTRL, "smmuv3.IRQ_CTRL.GERROR_IRQEN", 0, 1)
        , IRQ_CTRL_PRI_IRQEN(IRQ_CTRL, "smmuv3.IRQ_CTRL.PRI_IRQEN", 1, 1)
        , IRQ_CTRL_EVENTQ_IRQEN(IRQ_CTRL, "smmuv3.IRQ_CTRL.EVENTQ_IRQEN", 2, 1)
        , IRQ_CTRL_ACK("IRQ_CTRL_ACK", "smmuv3.IRQ_CTRL_ACK", 0x54, 1)
        , GERROR("GERROR", "smmuv3.GERROR", 0x60, 1)
        , GERROR_CMDQ_ERR(GERROR, "smmuv3.GERROR.CMDQ_ERR", 0, 1)
        , GERROR_EVENTQ_ABT_ERR(GERROR, "smmuv3.GERROR.EVENTQ_ABT_ERR", 2, 1)
        , GERROR_PRIQ_ABT_ERR(GERROR, "smmuv3.GERROR.PRIQ_ABT_ERR", 3, 1)
        , GERROR_MSI_CMDQ_ABT_ERR(GERROR, "smmuv3.GERROR.MSI_CMDQ_ABT_ERR", 4, 1)
        , GERROR_MSI_EVENTQ_ABT_ERR(GERROR, "smmuv3.GERROR.MSI_EVENTQ_ABT_ERR", 5, 1)
        , GERROR_MSI_PRIQ_ABT_ERR(GERROR, "smmuv3.GERROR.MSI_PRIQ_ABT_ERR", 6, 1)
        , GERROR_MSI_GERROR_ABT_ERR(GERROR, "smmuv3.GERROR.MSI_GERROR_ABT_ERR", 7, 1)
        , GERROR_SFM_ERR(GERROR, "smmuv3.GERROR.SFM_ERR", 8, 1)
        , GERRORN("GERRORN", "smmuv3.GERRORN", 0x64, 1)
        , GERROR_IRQ_CFG0_LO("GERROR_IRQ_CFG0_LO", "smmuv3.GERROR_IRQ_CFG0_LO", 0x68, 1)
        , GERROR_IRQ_CFG0_HI("GERROR_IRQ_CFG0_HI", "smmuv3.GERROR_IRQ_CFG0_HI", 0x6C, 1)
        , GERROR_IRQ_CFG1("GERROR_IRQ_CFG1", "smmuv3.GERROR_IRQ_CFG1", 0x70, 1)
        , GERROR_IRQ_CFG2("GERROR_IRQ_CFG2", "smmuv3.GERROR_IRQ_CFG2", 0x74, 1)
        , STRTAB_BASE_LO("STRTAB_BASE_LO", "smmuv3.STRTAB_BASE_LO", 0x80, 1)
        , STRTAB_BASE_HI("STRTAB_BASE_HI", "smmuv3.STRTAB_BASE_HI", 0x84, 1)
        , STRTAB_BASE_CFG("STRTAB_BASE_CFG", "smmuv3.STRTAB_BASE_CFG", 0x88, 1)
        , STRTAB_BASE_CFG_FMT(STRTAB_BASE_CFG, "smmuv3.STRTAB_BASE_CFG.FMT", 16, 2)
        , STRTAB_BASE_CFG_SPLIT(STRTAB_BASE_CFG, "smmuv3.STRTAB_BASE_CFG.SPLIT", 6, 5)
        , STRTAB_BASE_CFG_LOG2SIZE(STRTAB_BASE_CFG, "smmuv3.STRTAB_BASE_CFG.LOG2SIZE", 0, 6)
        , CMDQ_BASE_LO("CMDQ_BASE_LO", "smmuv3.CMDQ_BASE_LO", 0x90, 1)
        , CMDQ_BASE_LOG2SIZE(CMDQ_BASE_LO, "smmuv3.CMDQ_BASE_LO.LOG2SIZE", 0, 5)
        , CMDQ_BASE_HI("CMDQ_BASE_HI", "smmuv3.CMDQ_BASE_HI", 0x94, 1)
        , CMDQ_PROD("CMDQ_PROD", "smmuv3.CMDQ_PROD", 0x98, 1)
        , CMDQ_PROD_WRP(CMDQ_PROD, "smmuv3.CMDQ_PROD.WRP", 19, 1)
        , CMDQ_PROD_RD(CMDQ_PROD, "smmuv3.CMDQ_PROD.RD", 0, 19)
        , CMDQ_CONS("CMDQ_CONS", "smmuv3.CMDQ_CONS", 0x9C, 1)
        , CMDQ_CONS_WRP(CMDQ_CONS, "smmuv3.CMDQ_CONS.WRP", 19, 1)
        , CMDQ_CONS_RD(CMDQ_CONS, "smmuv3.CMDQ_CONS.RD", 0, 19)
        , CMDQ_CONS_ERR(CMDQ_CONS, "smmuv3.CMDQ_CONS.ERR", 23, 7)
        , EVENTQ_BASE_LO("EVENTQ_BASE_LO", "smmuv3.EVENTQ_BASE_LO", 0xA0, 1)
        , EVENTQ_BASE_LOG2SIZE(EVENTQ_BASE_LO, "smmuv3.EVENTQ_BASE_LO.LOG2SIZE", 0, 5)
        , EVENTQ_BASE_HI("EVENTQ_BASE_HI", "smmuv3.EVENTQ_BASE_HI", 0xA4, 1)
        , EVENTQ_PROD("EVENTQ_PROD", "smmuv3.EVENTQ_PROD", 0xA8, 1)
        , EVENTQ_PROD_WRP(EVENTQ_PROD, "smmuv3.EVENTQ_PROD.WRP", 19, 1)
        , EVENTQ_PROD_RD(EVENTQ_PROD, "smmuv3.EVENTQ_PROD.RD", 0, 19)
        , EVENTQ_CONS("EVENTQ_CONS", "smmuv3.EVENTQ_CONS", 0xAC, 1)
        , EVENTQ_CONS_WRP(EVENTQ_CONS, "smmuv3.EVENTQ_CONS.WRP", 19, 1)
        , EVENTQ_CONS_RD(EVENTQ_CONS, "smmuv3.EVENTQ_CONS.RD", 0, 19)
        , EVENTQ_IRQ_CFG0_LO("EVENTQ_IRQ_CFG0_LO", "smmuv3.EVENTQ_IRQ_CFG0_LO", 0xB0, 1)
        , EVENTQ_IRQ_CFG0_HI("EVENTQ_IRQ_CFG0_HI", "smmuv3.EVENTQ_IRQ_CFG0_HI", 0xB4, 1)
        , EVENTQ_IRQ_CFG1("EVENTQ_IRQ_CFG1", "smmuv3.EVENTQ_IRQ_CFG1", 0xB8, 1)
        , EVENTQ_IRQ_CFG2("EVENTQ_IRQ_CFG2", "smmuv3.EVENTQ_IRQ_CFG2", 0xBC, 1)
        , PRIQ_BASE_LO("PRIQ_BASE_LO", "smmuv3.PRIQ_BASE_LO", 0xC0, 1)
        , PRIQ_BASE_LOG2SIZE(PRIQ_BASE_LO, "smmuv3.PRIQ_BASE_LO.LOG2SIZE", 0, 5)
        , PRIQ_BASE_HI("PRIQ_BASE_HI", "smmuv3.PRIQ_BASE_HI", 0xC4, 1)
        , PRIQ_PROD("PRIQ_PROD", "smmuv3.PRIQ_PROD", 0xC8, 1)
        , PRIQ_PROD_WRP(PRIQ_PROD, "smmuv3.PRIQ_PROD.WRP", 19, 1)
        , PRIQ_PROD_RD(PRIQ_PROD, "smmuv3.PRIQ_PROD.RD", 0, 19)
        , PRIQ_CONS("PRIQ_CONS", "smmuv3.PRIQ_CONS", 0xCC, 1)
        , PRIQ_CONS_WRP(PRIQ_CONS, "smmuv3.PRIQ_CONS.WRP", 19, 1)
        , PRIQ_CONS_RD(PRIQ_CONS, "smmuv3.PRIQ_CONS.RD", 0, 19)
        , PRIQ_IRQ_CFG0_LO("PRIQ_IRQ_CFG0_LO", "smmuv3.PRIQ_IRQ_CFG0_LO", 0xD0, 1)
        , PRIQ_IRQ_CFG0_HI("PRIQ_IRQ_CFG0_HI", "smmuv3.PRIQ_IRQ_CFG0_HI", 0xD4, 1)
        , PRIQ_IRQ_CFG1("PRIQ_IRQ_CFG1", "smmuv3.PRIQ_IRQ_CFG1", 0xD8, 1)
        , PRIQ_IRQ_CFG2("PRIQ_IRQ_CFG2", "smmuv3.PRIQ_IRQ_CFG2", 0xDC, 1)
        , GATOS_CTRL("GATOS_CTRL", "smmuv3.GATOS_CTRL", 0x100, 1)
        , GATOS_CTRL_RUN(GATOS_CTRL, "smmuv3.GATOS_CTRL.RUN", 0, 1)
        , GATOS_CTRL_INPRG(GATOS_CTRL, "smmuv3.GATOS_CTRL.INPRG", 1, 1)
        , GATOS_CTRL_READ_NWRITE(GATOS_CTRL, "smmuv3.GATOS_CTRL.READ_NWRITE", 8, 1)
        , GATOS_SID("GATOS_SID", "smmuv3.GATOS_SID", 0x108, 1)
        , GATOS_ADDR_LO("GATOS_ADDR_LO", "smmuv3.GATOS_ADDR_LO", 0x110, 1)
        , GATOS_ADDR_HI("GATOS_ADDR_HI", "smmuv3.GATOS_ADDR_HI", 0x114, 1)
        , GATOS_PAR_LO("GATOS_PAR_LO", "smmuv3.GATOS_PAR_LO", 0x118, 1)
        , GATOS_PAR_F(GATOS_PAR_LO, "smmuv3.GATOS_PAR_LO.F", 0, 1)
        , GATOS_PAR_FST(GATOS_PAR_LO, "smmuv3.GATOS_PAR_LO.FST", 1, 7)
        , GATOS_PAR_HI("GATOS_PAR_HI", "smmuv3.GATOS_PAR_HI", 0x11C, 1)
        , IDREGS(12)
        , CMDQ_PROD_P1("CMDQ_PROD_P1", "smmuv3.CMDQ_PROD_P1", 0x10098, 1)
        , CMDQ_CONS_P1("CMDQ_CONS_P1", "smmuv3.CMDQ_CONS_P1", 0x1009C, 1)
        , S_IDR0("S_IDR0", "smmuv3.S_IDR0", 0x8000, 1)
        , S_IDR1("S_IDR1", "smmuv3.S_IDR1", 0x8004, 1)
        , S_IDR2("S_IDR2", "smmuv3.S_IDR2", 0x8008, 1)
        , S_IDR3("S_IDR3", "smmuv3.S_IDR3", 0x800C, 1)
        , S_IDR4("S_IDR4", "smmuv3.S_IDR4", 0x8010, 1)
        , S_CR0("S_CR0", "smmuv3.S_CR0", 0x8020, 1)
        , S_CR0_SMMUEN(S_CR0, "smmuv3.S_CR0.SMMUEN", 0, 1)
        , S_CR0_EVENTQEN(S_CR0, "smmuv3.S_CR0.EVENTQEN", 2, 1)
        , S_CR0_CMDQEN(S_CR0, "smmuv3.S_CR0.CMDQEN", 3, 1)
        , S_CR0ACK("S_CR0ACK", "smmuv3.S_CR0ACK", 0x8024, 1)
        , S_CR1("S_CR1", "smmuv3.S_CR1", 0x8028, 1)
        , S_CR2("S_CR2", "smmuv3.S_CR2", 0x802C, 1)
        , S_INIT("S_INIT", "smmuv3.S_INIT", 0x803C, 1)
        , S_INIT_INV_ALL(S_INIT, "smmuv3.S_INIT.INV_ALL", 0, 1)
        , S_GBPA("S_GBPA", "smmuv3.S_GBPA", 0x8044, 1)
        , S_GBPA_UPDATE(S_GBPA, "smmuv3.S_GBPA.UPDATE", 31, 1)
        , S_GBPA_ABORT(S_GBPA, "smmuv3.S_GBPA.ABORT", 20, 1)
        , S_AGBPA("S_AGBPA", "smmuv3.S_AGBPA", 0x8048, 1)
        , S_IRQ_CTRL("S_IRQ_CTRL", "smmuv3.S_IRQ_CTRL", 0x8050, 1)
        , S_IRQ_CTRL_GERROR_IRQEN(S_IRQ_CTRL, "smmuv3.S_IRQ_CTRL.GERROR_IRQEN", 0, 1)
        , S_IRQ_CTRL_EVENTQ_IRQEN(S_IRQ_CTRL, "smmuv3.S_IRQ_CTRL.EVENTQ_IRQEN", 2, 1)
        , S_IRQ_CTRL_ACK("S_IRQ_CTRL_ACK", "smmuv3.S_IRQ_CTRL_ACK", 0x8054, 1)
        , S_GERROR("S_GERROR", "smmuv3.S_GERROR", 0x8060, 1)
        , S_GERROR_CMDQ_ERR(S_GERROR, "smmuv3.S_GERROR.CMDQ_ERR", 0, 1)
        , S_GERROR_EVENTQ_ABT_ERR(S_GERROR, "smmuv3.S_GERROR.EVENTQ_ABT_ERR", 2, 1)
        , S_GERROR_MSI_CMDQ_ABT_ERR(S_GERROR, "smmuv3.S_GERROR.MSI_CMDQ_ABT_ERR", 4, 1)
        , S_GERROR_MSI_EVENTQ_ABT_ERR(S_GERROR, "smmuv3.S_GERROR.MSI_EVENTQ_ABT_ERR", 5, 1)
        , S_GERROR_MSI_GERROR_ABT_ERR(S_GERROR, "smmuv3.S_GERROR.MSI_GERROR_ABT_ERR", 7, 1)
        , S_GERROR_SFM_ERR(S_GERROR, "smmuv3.S_GERROR.SFM_ERR", 8, 1)
        , S_GERRORN("S_GERRORN", "smmuv3.S_GERRORN", 0x8064, 1)
        , S_GERROR_IRQ_CFG0_LO("S_GERROR_IRQ_CFG0_LO", "smmuv3.S_GERROR_IRQ_CFG0_LO", 0x8068, 1)
        , S_GERROR_IRQ_CFG0_HI("S_GERROR_IRQ_CFG0_HI", "smmuv3.S_GERROR_IRQ_CFG0_HI", 0x806C, 1)
        , S_GERROR_IRQ_CFG1("S_GERROR_IRQ_CFG1", "smmuv3.S_GERROR_IRQ_CFG1", 0x8070, 1)
        , S_GERROR_IRQ_CFG2("S_GERROR_IRQ_CFG2", "smmuv3.S_GERROR_IRQ_CFG2", 0x8074, 1)
        , S_STRTAB_BASE_LO("S_STRTAB_BASE_LO", "smmuv3.S_STRTAB_BASE_LO", 0x8080, 1)
        , S_STRTAB_BASE_HI("S_STRTAB_BASE_HI", "smmuv3.S_STRTAB_BASE_HI", 0x8084, 1)
        , S_STRTAB_BASE_CFG("S_STRTAB_BASE_CFG", "smmuv3.S_STRTAB_BASE_CFG", 0x8088, 1)
        , S_STRTAB_BASE_CFG_FMT(S_STRTAB_BASE_CFG, "smmuv3.S_STRTAB_BASE_CFG.FMT", 16, 2)
        , S_STRTAB_BASE_CFG_SPLIT(S_STRTAB_BASE_CFG, "smmuv3.S_STRTAB_BASE_CFG.SPLIT", 6, 5)
        , S_STRTAB_BASE_CFG_LOG2SIZE(S_STRTAB_BASE_CFG, "smmuv3.S_STRTAB_BASE_CFG.LOG2SIZE", 0, 6)
        , S_CMDQ_BASE_LO("S_CMDQ_BASE_LO", "smmuv3.S_CMDQ_BASE_LO", 0x8090, 1)
        , S_CMDQ_BASE_LOG2SIZE(S_CMDQ_BASE_LO, "smmuv3.S_CMDQ_BASE_LO.LOG2SIZE", 0, 5)
        , S_CMDQ_BASE_HI("S_CMDQ_BASE_HI", "smmuv3.S_CMDQ_BASE_HI", 0x8094, 1)
        , S_CMDQ_PROD("S_CMDQ_PROD", "smmuv3.S_CMDQ_PROD", 0x8098, 1)
        , S_CMDQ_PROD_WRP(S_CMDQ_PROD, "smmuv3.S_CMDQ_PROD.WRP", 19, 1)
        , S_CMDQ_PROD_RD(S_CMDQ_PROD, "smmuv3.S_CMDQ_PROD.RD", 0, 19)
        , S_CMDQ_CONS("S_CMDQ_CONS", "smmuv3.S_CMDQ_CONS", 0x809C, 1)
        , S_CMDQ_CONS_WRP(S_CMDQ_CONS, "smmuv3.S_CMDQ_CONS.WRP", 19, 1)
        , S_CMDQ_CONS_RD(S_CMDQ_CONS, "smmuv3.S_CMDQ_CONS.RD", 0, 19)
        , S_CMDQ_CONS_ERR(S_CMDQ_CONS, "smmuv3.S_CMDQ_CONS.ERR", 23, 7)
        , S_EVENTQ_BASE_LO("S_EVENTQ_BASE_LO", "smmuv3.S_EVENTQ_BASE_LO", 0x80A0, 1)
        , S_EVENTQ_BASE_LOG2SIZE(S_EVENTQ_BASE_LO, "smmuv3.S_EVENTQ_BASE_LO.LOG2SIZE", 0, 5)
        , S_EVENTQ_BASE_HI("S_EVENTQ_BASE_HI", "smmuv3.S_EVENTQ_BASE_HI", 0x80A4, 1)
        , S_EVENTQ_PROD("S_EVENTQ_PROD", "smmuv3.S_EVENTQ_PROD", 0x80A8, 1)
        , S_EVENTQ_PROD_WRP(S_EVENTQ_PROD, "smmuv3.S_EVENTQ_PROD.WRP", 19, 1)
        , S_EVENTQ_PROD_RD(S_EVENTQ_PROD, "smmuv3.S_EVENTQ_PROD.RD", 0, 19)
        , S_EVENTQ_CONS("S_EVENTQ_CONS", "smmuv3.S_EVENTQ_CONS", 0x80AC, 1)
        , S_EVENTQ_CONS_WRP(S_EVENTQ_CONS, "smmuv3.S_EVENTQ_CONS.WRP", 19, 1)
        , S_EVENTQ_CONS_RD(S_EVENTQ_CONS, "smmuv3.S_EVENTQ_CONS.RD", 0, 19)
        , S_EVENTQ_IRQ_CFG0_LO("S_EVENTQ_IRQ_CFG0_LO", "smmuv3.S_EVENTQ_IRQ_CFG0_LO", 0x80B0, 1)
        , S_EVENTQ_IRQ_CFG0_HI("S_EVENTQ_IRQ_CFG0_HI", "smmuv3.S_EVENTQ_IRQ_CFG0_HI", 0x80B4, 1)
        , S_EVENTQ_IRQ_CFG1("S_EVENTQ_IRQ_CFG1", "smmuv3.S_EVENTQ_IRQ_CFG1", 0x80B8, 1)
        , S_EVENTQ_IRQ_CFG2("S_EVENTQ_IRQ_CFG2", "smmuv3.S_EVENTQ_IRQ_CFG2", 0x80BC, 1)
        , EVENTQ_PROD_P1("EVENTQ_PROD_P1", "smmuv3.EVENTQ_PROD_P1", 0x100A8, 1)
        , EVENTQ_CONS_P1("EVENTQ_CONS_P1", "smmuv3.EVENTQ_CONS_P1", 0x100AC, 1)
        , PRIQ_PROD_P1("PRIQ_PROD_P1", "smmuv3.PRIQ_PROD_P1", 0x100C8, 1)
        , PRIQ_CONS_P1("PRIQ_CONS_P1", "smmuv3.PRIQ_CONS_P1", 0x100CC, 1)
    {
        for (uint32_t i = 0; i < 12; i++) {
            IDREGS[i] = std::make_shared<gs_register<uint32_t>>("IDREG" + std::to_string(i),
                                                                "smmuv3.IDREG" + std::to_string(i), 0xFD0 + (i * 4), 1);
        }
    }

    void bind_regs(gs::json_module& jm)
    {
        jm.bind_reg(IDR0);
        jm.bind_reg(IDR1);
        jm.bind_reg(IDR2);
        jm.bind_reg(IDR3);
        jm.bind_reg(IDR4);
        jm.bind_reg(IDR5);
        jm.bind_reg(IIDR);
        jm.bind_reg(AIDR);
        jm.bind_reg(CR0);
        jm.bind_reg(CR0ACK);
        jm.bind_reg(CR1);
        jm.bind_reg(CR2);
        jm.bind_reg(STATUSR);
        jm.bind_reg(GBPA);
        jm.bind_reg(AGBPA);
        jm.bind_reg(IRQ_CTRL);
        jm.bind_reg(IRQ_CTRL_ACK);
        jm.bind_reg(GERROR);
        jm.bind_reg(GERRORN);
        jm.bind_reg(GERROR_IRQ_CFG0_LO);
        jm.bind_reg(GERROR_IRQ_CFG0_HI);
        jm.bind_reg(GERROR_IRQ_CFG1);
        jm.bind_reg(GERROR_IRQ_CFG2);
        jm.bind_reg(STRTAB_BASE_LO);
        jm.bind_reg(STRTAB_BASE_HI);
        jm.bind_reg(STRTAB_BASE_CFG);
        jm.bind_reg(CMDQ_BASE_LO);
        jm.bind_reg(CMDQ_BASE_HI);
        jm.bind_reg(CMDQ_PROD);
        jm.bind_reg(CMDQ_CONS);
        jm.bind_reg(EVENTQ_BASE_LO);
        jm.bind_reg(EVENTQ_BASE_HI);
        jm.bind_reg(EVENTQ_PROD);
        jm.bind_reg(EVENTQ_CONS);
        jm.bind_reg(EVENTQ_IRQ_CFG0_LO);
        jm.bind_reg(EVENTQ_IRQ_CFG0_HI);
        jm.bind_reg(EVENTQ_IRQ_CFG1);
        jm.bind_reg(EVENTQ_IRQ_CFG2);
        jm.bind_reg(PRIQ_BASE_LO);
        jm.bind_reg(PRIQ_BASE_HI);
        jm.bind_reg(PRIQ_PROD);
        jm.bind_reg(PRIQ_CONS);
        jm.bind_reg(PRIQ_IRQ_CFG0_LO);
        jm.bind_reg(PRIQ_IRQ_CFG0_HI);
        jm.bind_reg(PRIQ_IRQ_CFG1);
        jm.bind_reg(PRIQ_IRQ_CFG2);
        jm.bind_reg(GATOS_CTRL);
        jm.bind_reg(GATOS_SID);
        jm.bind_reg(GATOS_ADDR_LO);
        jm.bind_reg(GATOS_ADDR_HI);
        jm.bind_reg(GATOS_PAR_LO);
        jm.bind_reg(GATOS_PAR_HI);

        for (auto& reg : IDREGS) {
            jm.bind_reg(*reg);
        }

        jm.bind_reg(CMDQ_PROD_P1);
        jm.bind_reg(CMDQ_CONS_P1);
        jm.bind_reg(EVENTQ_PROD_P1);
        jm.bind_reg(EVENTQ_CONS_P1);
        jm.bind_reg(PRIQ_PROD_P1);
        jm.bind_reg(PRIQ_CONS_P1);

        jm.bind_reg(S_IDR0);
        jm.bind_reg(S_IDR1);
        jm.bind_reg(S_IDR2);
        jm.bind_reg(S_IDR3);
        jm.bind_reg(S_IDR4);
        jm.bind_reg(S_CR0);
        jm.bind_reg(S_CR0ACK);
        jm.bind_reg(S_CR1);
        jm.bind_reg(S_CR2);
        jm.bind_reg(S_INIT);
        jm.bind_reg(S_GBPA);
        jm.bind_reg(S_AGBPA);
        jm.bind_reg(S_IRQ_CTRL);
        jm.bind_reg(S_IRQ_CTRL_ACK);
        jm.bind_reg(S_GERROR);
        jm.bind_reg(S_GERRORN);
        jm.bind_reg(S_GERROR_IRQ_CFG0_LO);
        jm.bind_reg(S_GERROR_IRQ_CFG0_HI);
        jm.bind_reg(S_GERROR_IRQ_CFG1);
        jm.bind_reg(S_GERROR_IRQ_CFG2);
        jm.bind_reg(S_STRTAB_BASE_LO);
        jm.bind_reg(S_STRTAB_BASE_HI);
        jm.bind_reg(S_STRTAB_BASE_CFG);
        jm.bind_reg(S_CMDQ_BASE_LO);
        jm.bind_reg(S_CMDQ_BASE_HI);
        jm.bind_reg(S_CMDQ_PROD);
        jm.bind_reg(S_CMDQ_CONS);
        jm.bind_reg(S_EVENTQ_BASE_LO);
        jm.bind_reg(S_EVENTQ_BASE_HI);
        jm.bind_reg(S_EVENTQ_PROD);
        jm.bind_reg(S_EVENTQ_CONS);
        jm.bind_reg(S_EVENTQ_IRQ_CFG0_LO);
        jm.bind_reg(S_EVENTQ_IRQ_CFG0_HI);
        jm.bind_reg(S_EVENTQ_IRQ_CFG1);
        jm.bind_reg(S_EVENTQ_IRQ_CFG2);

        jm.log_end_of_binding_msg("smmuv3_gen");
    }
};

} // namespace gs

#endif
