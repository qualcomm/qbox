/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef smmu500_H
#define smmu500_H

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

#include <cassert>
#include <cstdint>
#include <mutex>
#include <vector>
#include <memory>
#include <sstream>

#include "smmu500_gen.h"

#define SMMU_PAGESIZE 4096
#define SMMU_PAGEMASK (SMMU_PAGESIZE - 1)
#define SMMU_MAX_CB   128
#define SMMU_MAX_TBU  16
#define SMMU_VA_WIDTH 32
#define SMMU_ADDRMASK ((1ULL << 12) - 1)

/* Theoretically a DMI request can be failed with no ill effects, and to protect against re-entrant code
 * between a DMI invalidate and a DMI request on separate threads, effectively requiring work to be done
 * on the same thread, we make use of this by 'try_lock'ing and failing the DMI. However this has the
 * negative side effect that up-stream models my not get DMI's when they expect them, which at the very
 * least would be a performance hit.
 * Define this as true if you require protection against re-entrant models.
 */
#define THREAD_SAFE_REENTRANT false

namespace gs {

template <unsigned int BUSWIDTH = 32>
class smmu500_tbu;

template <unsigned int BUSWIDTH = 32>
class smmu500 : public sc_core::sc_module, public smmu500_gen
{
    SCP_LOGGER();
    cci::cci_broker_handle m_broker;
    gs::json_zip_archive m_jza;
    bool loaded_ok;
    gs::json_module M;

public:
    typedef enum {
        IOMMU_NONE = 0,
        IOMMU_RO = 1,
        IOMMU_WO = 2,
        IOMMU_RW = 3,
    } IOMMUAccessFlags;

    struct IOMMUTLBEntry {
        uint64_t iova;
        uint64_t translated_addr;
        uint64_t addr_mask;
        IOMMUAccessFlags perm;
    };

    cci::cci_param<uint32_t> p_pamax;
    cci::cci_param<uint16_t> p_num_smr;
    cci::cci_param<uint16_t> p_num_cb;
    cci::cci_param<uint16_t> p_num_pages;
    cci::cci_param<bool> p_ato;
    cci::cci_param<uint8_t> p_version;
    cci::cci_param<uint8_t> p_num_tbu;

    tlm_utils::multi_passthrough_target_socket<smmu500, BUSWIDTH> socket;
    tlm_utils::simple_initiator_socket<smmu500> dma_socket;
    InitiatorSignalSocket<bool> irq_global;
    sc_core::sc_vector<InitiatorSignalSocket<bool>> irq_context;
    TargetSignalSocket<bool> reset;

    std::vector<smmu500_tbu<BUSWIDTH>*> tbus;

    smmu500(sc_core::sc_module_name name);

private:
    typedef struct PageAttr {
        uint64_t pa;
        unsigned int block : 1;
        unsigned int rd : 1;
        unsigned int wr : 1;
        unsigned int ns : 1;
    } PageAttr;

    typedef struct TransReq {
        uint64_t va;
        uint64_t tcr[3];
        uint64_t ttbr[3][2];
        uint32_t access;
        unsigned int stage;
        bool s2_enabled;
        unsigned int s2_cb;
        uint64_t pa;
        uint32_t prot;
        uint64_t page_size;
        bool err;
    } TransReq;

    static uint32_t extract32(uint32_t val, int start, int length) { return (val >> start) & ((1u << length) - 1); }

    static uint64_t extract64(uint64_t val, int start, int length) { return (val >> start) & ((1ULL << length) - 1); }

    static int clz32(uint32_t val) { return val ? __builtin_clz(val) : 32; }

    void smmu500_update_ctx_irq(unsigned int cb)
    {
        bool tf = (*SMMU_CB_FSR[cb])[CB_FSR_TF];
        bool ie = (*SMMU_CB_SCTLR[cb])[CB_SCTLR_CFIE];
        irq_context[cb]->write(tf && ie);
    }

    void smmu500_fault(unsigned int cb, TransReq* req, uint64_t syn)
    {
        (*SMMU_CB_FSR[cb])[CB_FSR_TF] = 1;
        req->err = true;
        *SMMU_CB_IPAFAR_LOW[cb] = (uint32_t)req->va;
        *SMMU_CB_IPAFAR_HIGH[cb] = (uint32_t)(req->va >> 32);
        if (req->stage == 2) {
            *SMMU_CB_FAR_LOW[cb] = (uint32_t)req->va;
            *SMMU_CB_FAR_HIGH[cb] = (uint32_t)(req->va >> 32);
        }
        smmu500_update_ctx_irq(cb);
    }

    bool check_s2_startlevel(bool is_aa64, unsigned int pamax_val, int level, int inputsize, int stride)
    {
        if (level < 0) return false;
        if (is_aa64) {
            switch (stride) {
            case 13:
                if (level == 0 || (level == 1 && pamax_val <= 42)) return false;
                break;
            case 11:
                if (level == 0 || (level == 1 && pamax_val <= 40)) return false;
                break;
            case 9:
                if (level == 0 && pamax_val <= 42) return false;
                break;
            default:
                assert(false);
            }
        } else {
            const int grainsize = stride + 3;
            assert(stride == 9);
            if (level == 0) return false;
            int startsizecheck = inputsize - ((3 - level) * stride + grainsize);
            if (startsizecheck < 1 || startsizecheck > stride + 4) return false;
        }
        return true;
    }

    bool check_out_addr(uint64_t addr, unsigned int outputsize)
    {
        if (outputsize != 48 && extract64(addr, outputsize, 48 - outputsize)) return false;
        return true;
    }

    void dump_trans_req(TransReq const& tr)
    {
        SCP_DEBUG(()) << "Translation Req\n"
                      << std::hex << "VA: 0x" << tr.va << "\n"
                      << "TCR[0]: 0x" << tr.tcr[0] << "\n"
                      << "TCR[1]: 0x" << tr.tcr[1] << "\n"
                      << "TCR[2]: 0x" << tr.tcr[2] << "\n"
                      << "ACCESS: 0x" << tr.access << "\n"
                      << "Stage: " << std::dec << tr.stage << "\n"
                      << "S2_enabled: " << (tr.s2_enabled ? "true" : "false") << "\n"
                      << "S2_CB: " << tr.s2_cb << "\n"
                      << std::hex << "PA: 0x" << tr.pa << "\n"
                      << "Prot: 0x" << tr.prot << "\n"
                      << "Page size: 0x" << tr.page_size << "\n"
                      << "Error: " << (tr.err ? "true" : "false") << "\n";
    }

    void dump_cb_state(unsigned int cb)
    {
        SCP_DEBUG(()) << "CB" << cb << ":\n"
                      << std::hex << "CB_SCTLR = 0x" << (uint32_t)*SMMU_CB_SCTLR[cb] << "\n"
                      << "CB_TCR2 = 0x" << (uint32_t)*SMMU_CB_TCR2[cb] << "\n"
                      << "CB_TTBR0_LOW = 0x" << (uint32_t)*SMMU_CB_TTBR0_LOW[cb] << "\n"
                      << "CB_TTBR0_HIGH = 0x" << (uint32_t)*SMMU_CB_TTBR0_HIGH[cb] << "\n"
                      << "CB_TTBR1_LOW = 0x" << (uint32_t)*SMMU_CB_TTBR1_LOW[cb] << "\n"
                      << "CB_TTBR1_HIGH = 0x" << (uint32_t)*SMMU_CB_TTBR1_HIGH[cb] << "\n"
                      << "CB_TCR_LPAE = 0x" << (uint32_t)*SMMU_CB_TCR_LPAE[cb] << "\n"
                      << "CB_FSR = 0x" << (uint32_t)*SMMU_CB_FSR[cb] << "\n"
                      << "CB_FAR_LOW = 0x" << (uint32_t)*SMMU_CB_FAR_LOW[cb] << "\n"
                      << "CB_FAR_HIGH = 0x" << (uint32_t)*SMMU_CB_FAR_HIGH[cb] << "\n"
                      << "CB_IPAFAR_LOW = 0x" << (uint32_t)*SMMU_CB_IPAFAR_LOW[cb] << "\n"
                      << "CB_IPAFAR_HIGH = 0x" << (uint32_t)*SMMU_CB_IPAFAR_HIGH[cb] << "\n";
    }

    void dump_state()
    {
        SCP_DEBUG(()) << "smmu regs:" << std::hex << "SMMU_SCR0 = 0x" << (uint32_t)SMMU_SCR0 << "\n"
                      << "SMMU_SCR1 = 0x" << (uint32_t)SMMU_SCR1 << "\n"
                      << "SMMU_SACR = 0x" << (uint32_t)SMMU_SACR << "\n"
                      << "SMMU_SIDR0 = 0x" << (uint32_t)SMMU_SIDR0 << "\n"
                      << "SMMU_SIDR1 = 0x" << (uint32_t)SMMU_SIDR1 << "\n"
                      << "SMMU_SIDR2 = 0x" << (uint32_t)SMMU_SIDR2 << "\n"
                      << "SMMU_SIDR7 = 0x" << (uint32_t)SMMU_SIDR7 << "\n"
                      << "SMMU_NSCR0 = 0x" << (uint32_t)SMMU_NSCR0 << "\n";
    }

    void smmu500_ptw64(unsigned int cb, TransReq* req)
    {
        const unsigned int outsize_map[] = {
            [0] = 32, [1] = 36, [2] = 40, [3] = 42, [4] = 44, [5] = 48, [6] = 48, [7] = 48,
        };
        unsigned int tsz;
        unsigned int t0sz;
        unsigned int t1sz;
        unsigned int inputsize;
        unsigned int outputsize;
        unsigned int grainsize = -1;
        unsigned int stride;
        int level = 0;
        unsigned int firstblocklevel = 0;
        unsigned int tg;
        unsigned int ps;
        unsigned int baselowerbound;
        unsigned int stage = req->stage;
        bool blocktranslate = false;
        bool epd = false;
        bool va64;
        bool type64;
        uint32_t tableattrs = 0;
        uint32_t attrs;
        uint32_t s2attrs;
        uint64_t descmask;
        uint64_t ttbr;
        uint64_t desc;

        req->err = false;

        if ((*SMMU_CB_SCTLR[cb])[CB_SCTLR_M] == 0) {
            req->pa = req->va;
            req->prot = IOMMU_RW;
            SCP_INFO(()) << "SMMU disabled for context " << cb << " sctlr=0x" << std::hex
                         << (uint32_t)*SMMU_CB_SCTLR[cb];
            return;
        }

        ttbr = req->ttbr[stage][0];
        tg = extract32(req->tcr[stage], 14, 2);
        if (stage == 1) {
            ps = extract64(req->tcr[stage], 32, 3);
        } else {
            ps = extract64(req->tcr[stage], 16, 3);
        }
        t0sz = extract32(req->tcr[stage], 0, 6);
        tsz = t0sz;
        req->pa = req->va;

        va64 = SMMU_CBA2R[cb][CBA2R_VA64];
        if (req->stage == 1) {
            type64 = va64 || extract32(req->tcr[1], 31, 1);
            assert(type64);

            if ((req->va & (1ULL << 63)) == 0) {
                /* Use TTBR0 */
            } else {
                const unsigned int tg1map[] = { [0] = 3, [1] = 2, [2] = 0, [3] = 1 };
                if (!va64 && type64) {
                    tg = extract32(req->tcr[stage], 30, 2);
                    tg = tg1map[tg];
                    t1sz = extract32(req->tcr[stage], 16, 6);
                } else {
                    tg = 0;
                    t1sz = extract32(req->tcr[stage], 16, 3);
                }
                ttbr = req->ttbr[stage][1];
                tsz = t1sz;
            }
            epd = extract32(req->tcr[1], 7, 1);
        } else {
            type64 = true;
        }

        if (epd) goto do_fault;

        inputsize = 64 - tsz;
        switch (tg) {
        case 1:
            grainsize = 16;
            level = 3;
            firstblocklevel = 2;
            break;
        case 2:
            grainsize = 14;
            level = 3;
            firstblocklevel = 2;
            break;
        case 0:
            grainsize = 12;
            level = 2;
            firstblocklevel = 1;
            break;
        default:
            SCP_ERR(()) << "Wrong pagesize";
            break;
        }

        outputsize = outsize_map[ps];
        if (outputsize > p_pamax) outputsize = p_pamax;

        stride = grainsize - 3;
        if (req->stage == 1) {
            if (grainsize < 16 && (inputsize > (grainsize + 3 * stride)))
                level = 0;
            else if (inputsize > (grainsize + 2 * stride))
                level = 1;
            else if (inputsize > (grainsize + stride))
                level = 2;

            if (inputsize < 25 || inputsize > 48 || extract64(req->va, inputsize, 64 - inputsize)) goto do_fault;
        } else {
            unsigned int startlevel = extract32(req->tcr[stage], 6, 2);
            level = 3 - startlevel;
            if (grainsize == 12) level = 2 - startlevel;
            if (!check_s2_startlevel(true, outputsize, level, inputsize, stride)) goto do_fault;
        }

        baselowerbound = 3 + inputsize - ((3 - level) * stride + grainsize);
        ttbr = extract64(ttbr, 0, 48);
        ttbr &= ~((1ULL << baselowerbound) - 1);

        if (!check_out_addr(ttbr, outputsize)) goto do_fault;

        descmask = (1ULL << grainsize) - 1;
        do {
            unsigned int addrselectbottom = (3 - level) * stride + grainsize;
            uint64_t index = (req->va >> (addrselectbottom - 3)) & descmask;
            index &= ~7ULL;
            uint64_t descaddr = ttbr | index;

            if (req->stage == 1 && req->s2_enabled) {
                TransReq s2req = *req;
                s2req.stage = 2;
                s2req.va = descaddr;
                smmu500_ptw64(s2req.s2_cb, &s2req);
                if (s2req.err) {
                    *SMMU_CB_IPAFAR_LOW[cb] = (uint32_t)descaddr;
                    *SMMU_CB_IPAFAR_HIGH[cb] = (uint32_t)(descaddr >> 32);
                    goto do_fault;
                }
                descaddr = s2req.pa;
            }

            tlm::tlm_generic_payload txn;
            txn.set_command(tlm::TLM_READ_COMMAND);
            txn.set_address(descaddr);
            txn.set_data_ptr(reinterpret_cast<unsigned char*>(&desc));
            txn.set_data_length(sizeof(desc));
            txn.set_streaming_width(sizeof(desc));
            txn.set_byte_enable_length(0);
            txn.set_dmi_allowed(false);
            txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

            sc_core::sc_time now = sc_core::sc_time_stamp();
            dma_socket->b_transport(txn, now);

            if (txn.get_response_status() != tlm::TLM_OK_RESPONSE) {
                SCP_INFO(()) << "Bad DMA response";
                goto do_fault;
            }

            unsigned int type = desc & 3;
            SCP_INFO(()) << "S" << req->stage << " L" << level << " va=0x" << std::hex << req->va << " gz=0x"
                         << grainsize << " descaddr=0x" << descaddr << " desc=0x" << desc << " asb=0x"
                         << addrselectbottom << " index=0x" << index << " osize=0x" << outputsize;

            ttbr = extract64(desc, 0, 48);
            ttbr &= ~descmask;

            if (!(type & 2) && level == 3) {
                SCP_INFO(()) << "bad level 3 desc";
                goto do_fault;
            }

            if (level == 3) break;

            switch (type) {
            case 2:
            case 0:
                SCP_INFO(()) << "bad desc";
                goto do_fault;
                break;
            case 1:
                blocktranslate = true;
                if (level < (int)firstblocklevel) goto do_fault;
                break;
            case 3:
                tableattrs |= extract64(desc, 59, 5);
                if (!check_out_addr(ttbr, outputsize)) goto do_fault;
                level++;
                break;
            }
        } while (!blocktranslate);

        if (!check_out_addr(ttbr, outputsize)) goto do_fault;

        {
            unsigned long page_size;
            page_size = (1ULL << ((stride * (4 - level)) + 3));
            ttbr |= (req->va & (page_size - 1));
            req->page_size = ((stride * (4 - level)) + 3);
        }

        s2attrs = attrs = extract64(desc, 2, 10) | (extract64(desc, 52, 12) << 10);
        if (req->stage == 1) {
            attrs |= extract32(tableattrs, 0, 2) << 11;
            attrs |= extract32(tableattrs, 3, 1) << 5;
            if (0 && extract32(tableattrs, 2, 1)) {
                attrs &= ~(1 << 4);
            }
        }

        req->prot = IOMMU_RW;
        if ((attrs & (1 << 8)) == 0) {
            SCP_INFO(()) << "access forbidden " << std::hex << attrs;
            goto do_fault;
        }

        if (req->stage == 1) {
            if (!(attrs & (1 << 4))) {
                SCP_INFO(()) << "AP[1] should be one but set to zero!";
                goto do_fault;
            }
            if (attrs & (1 << 5)) {
                if (req->access == IOMMU_WO) {
                    SCP_INFO(()) << "Write access forbidden " << std::hex << attrs;
                    goto do_fault;
                }
                req->prot &= ~IOMMU_WO;
            }
        } else {
            switch ((s2attrs >> 4) & 3) {
            case 0:
                goto do_fault;
                break;
            case 1:
                if (req->access == IOMMU_WO) goto do_fault;
                req->prot &= ~IOMMU_WO;
                break;
            case 2:
                if (req->access == IOMMU_RO) goto do_fault;
                req->prot &= ~IOMMU_RO;
                break;
            case 3:
                break;
            }
        }

        req->pa = ttbr;
        SCP_INFO(()) << "0x" << std::hex << req->va << " -> 0x" << req->pa;
        return;

    do_fault:
        SCP_INFO(()) << "smmu fault CB" << cb;
        dump_trans_req(*req);
        dump_cb_state(cb);
        dump_state();
        smmu500_fault(cb, req, level);
    }

    bool smmu500_at64(unsigned int cb, uint64_t va, bool wr, bool s2, uint64_t* pa, int* prot, uint64_t* page_size)
    {
        unsigned int s2_cb = 0;
        TransReq req;
        unsigned int t;

        t = SMMU_CBAR[cb][CBAR_TYPE];

        switch (t) {
        case 0:
            req.stage = 2;
            req.s2_enabled = true;
            req.s2_cb = cb;
            s2_cb = cb;
            break;
        case 1:
            req.stage = 1;
            req.s2_enabled = false;
            break;
        case 2:
            req.stage = 1;
            req.s2_enabled = false;
            break;
        case 3:
            req.stage = 1;
            req.s2_enabled = true;
            req.s2_cb = SMMU_CBAR[cb][CBAR_VMID];
            s2_cb = req.s2_cb;
            break;
        }

        req.va = va;
        req.tcr[1] = (uint32_t)*SMMU_CB_TCR2[cb];
        req.tcr[1] <<= 32;
        req.tcr[1] |= (uint32_t)*SMMU_CB_TCR_LPAE[cb];

        req.ttbr[1][0] = (uint32_t)*SMMU_CB_TTBR0_HIGH[cb];
        req.ttbr[1][0] <<= 32;
        req.ttbr[1][0] |= (uint32_t)*SMMU_CB_TTBR0_LOW[cb];

        req.ttbr[1][1] = (uint32_t)*SMMU_CB_TTBR1_HIGH[cb];
        req.ttbr[1][1] <<= 32;
        req.ttbr[1][1] |= (uint32_t)*SMMU_CB_TTBR1_LOW[cb];

        if (req.s2_enabled) {
            req.tcr[2] = (uint32_t)*SMMU_CB_TCR_LPAE[s2_cb];
            req.ttbr[2][0] = (uint32_t)*SMMU_CB_TTBR0_HIGH[s2_cb];
            req.ttbr[2][0] <<= 32;
            req.ttbr[2][0] |= (uint32_t)*SMMU_CB_TTBR0_LOW[s2_cb];
        }

        req.access = wr ? IOMMU_WO : IOMMU_RO;
        req.page_size = *page_size;

        if (req.stage == 1) {
            smmu500_ptw64(cb, &req);
            req.stage++;
        } else {
            req.pa = req.va;
        }

        if (s2 && req.s2_enabled) {
            req.va = req.pa;
            smmu500_ptw64(cb, &req);
        }

        *pa = req.pa;
        *prot = req.prot;
        *page_size = req.page_size;
        return req.err;
    }

    bool smmu500_at(unsigned int cb, uint64_t va, bool wr, bool s2, uint64_t* pa, int* prot, uint64_t* page_size)
    {
        return smmu500_at64(cb, va, wr, s2, pa, prot, page_size);
    }

    void smmu500_gat(uint64_t v, bool wr, bool s2)
    {
        uint64_t va = (v & ~SMMU_ADDRMASK) & ((1ULL << SMMU_VA_WIDTH) - 1);
        unsigned int cb = v & SMMU_ADDRMASK;
        uint64_t pa;
        int prot;
        uint64_t page_size;

        SCP_INFO(()) << "ATS: va=0x" << std::hex << va << " cb=0x" << cb << " wr=" << wr << " s2=" << s2;
        bool err = smmu500_at(cb, va, wr, s2, &pa, &prot, &page_size);

        SMMU_GPAR = (uint32_t)(pa | err);
        SMMU_GPAR_H = (uint32_t)(pa >> 32);
    }

public:
    IOMMUTLBEntry smmu500_translate(tlm::tlm_generic_payload& txn, uint64_t sid)
    {
        uint64_t addr = txn.get_address();
        uint64_t page_size = 12;
        IOMMUTLBEntry ret = {
            .iova = 0,
            .translated_addr = addr,
            .addr_mask = (1ULL << page_size) - 1,
            .perm = IOMMU_RW,
        };
        int cb;
        uint64_t va = (addr & ~SMMU_ADDRMASK) & ((1ULL << SMMU_VA_WIDTH) - 1);
        uint64_t pa = va;
        int prot;
        bool err = false;
        uint16_t master_id = 0;
        master_id |= sid;
        master_id |= (addr >> 32) & 0xf;
        bool clientpd = SCR0_CLIENTPD;
        if (clientpd) {
            ret.addr_mask = -1;
            return ret;
        }

        cb = smmu500_stream_id_match(master_id);
        assert(cb < p_num_cb);
        if (cb >= 0) {
            bool wr = (txn.get_command() == tlm::TLM_WRITE_COMMAND);
            err = smmu500_at(cb, va, wr, true, &pa, &prot, &page_size);
            ret.translated_addr = pa;
            ret.perm = (IOMMUAccessFlags)prot;
            if (err) {
                memset(&ret, 0, sizeof ret);
                ret.perm = IOMMU_NONE;
            }
        } else {
            ret.addr_mask = -1;
        }
        return ret;
    }

    int smmu500_stream_id_match(uint32_t stream_id)
    {
        unsigned int nr_smr = SIDR0_NUMSMRG;
        unsigned int cbndx = -1;

        for (unsigned int i = 0; i < nr_smr; i++) {
            bool valid = SMMU_SMR[i][SMR_VALID];
            uint16_t mask = SMMU_SMR[i][SMR_MASK];
            uint16_t id = SMMU_SMR[i][SMR_ID];

            if (valid && (~mask & id) == (~mask & stream_id)) {
                cbndx = SMMU_S2CR[i][S2CR_CBNDX_VMID];
                break;
            }
        }

        SCP_INFO(()) << "SMMU StreamID 0x" << std::hex << stream_id << " -> CB 0x" << cbndx;
        return cbndx;
    }

    void start_of_simulation();
    void before_end_of_elaboration();
};

template <unsigned int BUSWIDTH>
class smmu500_tbu : public sc_core::sc_module
{
    SCP_LOGGER();
    smmu500<BUSWIDTH>* m_smmu;
    std::mutex m_dmi_invalidate_lock;

    std::pair<uint64_t, uint64_t> dmi_range[SMMU_MAX_CB] = {};
    bool dmi_range_valid[SMMU_MAX_CB] = { false };

    struct MemoryView {
        uint64_t address;
        uint64_t page_start;
        uint64_t page_end;
        uint64_t page_size;
    };

protected:
    void b_transport(tlm::tlm_generic_payload& txn, sc_core::sc_time& delay)
    {
        sc_dt::uint64 addr = txn.get_address();
        tlm::tlm_command cmd = txn.get_command();
        typename smmu500<BUSWIDTH>::IOMMUTLBEntry te = m_smmu->smmu500_translate(txn, p_topology_id);

        if (te.perm == smmu500<BUSWIDTH>::IOMMU_NONE ||
            (cmd == tlm::TLM_WRITE_COMMAND && te.perm == smmu500<BUSWIDTH>::IOMMU_RO) ||
            (cmd == tlm::TLM_READ_COMMAND && te.perm == smmu500<BUSWIDTH>::IOMMU_WO)) {
            txn.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        } else {
            txn.set_address(te.translated_addr | (addr & SMMU_PAGEMASK));
            SCP_INFO(()) << std::hex << "smmu TBU b_transport: translate 0x" << addr << " to 0x"
                         << (te.translated_addr | (addr & SMMU_PAGEMASK));
            downstream_socket->b_transport(txn, delay);
            txn.set_address(addr);
        }
    }

    virtual unsigned int transport_dbg(tlm::tlm_generic_payload& txn)
    {
        sc_dt::uint64 addr = txn.get_address();
        typename smmu500<BUSWIDTH>::IOMMUTLBEntry te = m_smmu->smmu500_translate(txn, p_topology_id);
        txn.set_address(te.translated_addr | (addr & SMMU_PAGEMASK));
        int ret = downstream_socket->transport_dbg(txn);
        txn.set_address(addr);
        return ret;
    }

    virtual bool get_direct_mem_ptr(tlm::tlm_generic_payload& txn, tlm::tlm_dmi& dmi_data)
    {
#if THREAD_SAFE_REENTRANT == true
        if (!m_dmi_invalidate_lock.try_lock()) {
            SCP_DEBUG(())("Failed to get lock, DMI will be refused");
            return false;
        }
#else
        m_dmi_invalidate_lock.lock();
#endif

        MemoryView VIRT;
        MemoryView PHYS;

        VIRT.address = txn.get_address();
        typename smmu500<BUSWIDTH>::IOMMUTLBEntry te = m_smmu->smmu500_translate(txn, p_topology_id);

        SCP_DEBUG(())("te iova {:x} translated_addr {:x} addr_mask {:x}", te.iova, te.translated_addr, te.addr_mask);

        if (te.addr_mask == (uint64_t)-1) {
            assert(te.translated_addr == VIRT.address);
            bool ret = downstream_socket->get_direct_mem_ptr(txn, dmi_data);
            VIRT.page_end = dmi_data.get_end_address();
            if (VIRT.page_end >> SMMU_VA_WIDTH != VIRT.address >> SMMU_VA_WIDTH) {
                dmi_data.set_end_address((((VIRT.address >> SMMU_VA_WIDTH) + 1) << SMMU_VA_WIDTH) - 1);
            }
            VIRT.page_start = dmi_data.get_start_address();
            if (VIRT.page_start >> SMMU_VA_WIDTH != VIRT.address >> SMMU_VA_WIDTH) {
                uint64_t newstart = (VIRT.address >> SMMU_VA_WIDTH) << SMMU_VA_WIDTH;
                dmi_data.set_start_address(newstart);
                dmi_data.set_dmi_ptr(dmi_data.get_dmi_ptr() + (newstart - VIRT.page_start));
            }
            m_dmi_invalidate_lock.unlock();
            return ret;
        }

        PHYS.address = te.translated_addr;
        PHYS.page_start = te.translated_addr & ~te.addr_mask;
        PHYS.page_size = (te.addr_mask + 1);
        PHYS.page_end = PHYS.page_start + PHYS.page_size;
        assert(PHYS.page_start < PHYS.page_end);

        txn.set_address(PHYS.page_start);

        int ret = downstream_socket->get_direct_mem_ptr(txn, dmi_data);
        if (!ret) {
            m_dmi_invalidate_lock.unlock();
            return ret;
        }

        gs::UnderlyingDMITlmExtension* u_dmi;
        txn.get_extension(u_dmi);
        if (u_dmi) {
            tlm::tlm_dmi udmi = dmi_data;
            u_dmi->add_dmi(this, udmi, gs::tlm_dmi_ex::dmi_iommu);
        }

        uint64_t dmi_offset = PHYS.page_start - dmi_data.get_start_address();
        assert(dmi_offset >= 0);
        assert(PHYS.page_end <= dmi_data.get_end_address());

        uint64_t ABS_physical_address = PHYS.address + (VIRT.address & SMMU_PAGEMASK);
        uint64_t device_offset = ABS_physical_address - PHYS.page_start;

        VIRT.page_start = VIRT.address - device_offset;
        VIRT.page_end = (VIRT.page_start + PHYS.page_size) - 1;

        dmi_data.set_dmi_ptr(dmi_data.get_dmi_ptr() + dmi_offset);
        dmi_data.set_start_address(VIRT.page_start);
        dmi_data.set_end_address(VIRT.page_end);
        dmi_data.allow_read_write();

        uint32_t master_id = p_topology_id;
        master_id |= (VIRT.address >> 32) & 0xf;
        int CB = m_smmu->smmu500_stream_id_match(master_id);
        if (!dmi_range_valid[CB] || dmi_range[CB].first > VIRT.page_start) {
            dmi_range[CB].first = VIRT.page_start;
        }
        if (!dmi_range_valid[CB] || dmi_range[CB].second < VIRT.page_end) {
            dmi_range[CB].second = VIRT.page_end;
        }
        dmi_range_valid[CB] = true;

        if (PHYS.page_start != VIRT.page_start) {
            uint64_t bits_masked = VIRT.address - (VIRT.address & ~SMMU_PAGEMASK);
            SCP_INFO(()) << std::hex << "\nVIRT   Page Start 0x" << VIRT.page_start << "\nPHYS   Page Start 0x"
                         << PHYS.page_start << "\nbits_masked         0x" << bits_masked;
        }
        SCP_INFO(()) << std::hex << "smmu TBU DMI: translate 0x" << VIRT.address << " to 0x" << te.translated_addr
                     << " pg size 0x" << PHYS.page_size << " pg base 0x" << PHYS.page_start << " offset 0x"
                     << dmi_offset << " start 0x" << dmi_data.get_start_address() << " end 0x"
                     << dmi_data.get_end_address();

        txn.set_address(VIRT.address);
        m_dmi_invalidate_lock.unlock();
        return ret;
    }

public:
    cci::cci_param<uint32_t> p_topology_id;

    tlm_utils::simple_target_socket<smmu500_tbu> upstream_socket;
    tlm_utils::simple_initiator_socket<smmu500_tbu> downstream_socket;

    smmu500_tbu(const sc_core::sc_module_name& name, sc_core::sc_object* o)
        : smmu500_tbu(name, dynamic_cast<smmu500<BUSWIDTH>*>(o))
    {
    }

    smmu500_tbu(sc_core::sc_module_name name, smmu500<BUSWIDTH>* _smmu)
        : p_topology_id("topology_id", 0x0, "Topology ID for this TBU")
        , upstream_socket("upstream_socket")
        , downstream_socket("downstream_socket")
    {
        m_smmu = _smmu;
        m_smmu->tbus.push_back(this);
        upstream_socket.register_b_transport(this, &smmu500_tbu::b_transport);
        upstream_socket.register_transport_dbg(this, &smmu500_tbu::transport_dbg);
        upstream_socket.register_get_direct_mem_ptr(this, &smmu500_tbu::get_direct_mem_ptr);
    }

    void start_invalidates() { m_dmi_invalidate_lock.lock(); }
    void stop_invalidates() { m_dmi_invalidate_lock.unlock(); }

    void invalidate(uint32_t CB)
    {
        if (dmi_range_valid[CB]) {
            SCP_INFO(())("TLBIALL invalidate {:x} - {:x}", dmi_range[CB].first, dmi_range[CB].second);
            upstream_socket->invalidate_direct_mem_ptr(dmi_range[CB].first, dmi_range[CB].second);
        }
        dmi_range_valid[CB] = false;
    }
};

} // namespace gs

extern "C" void module_register();

#endif // smmu500_H
