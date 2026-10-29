/*
 * This file is part of libqbox
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQBOX_PORTS_INITIATOR_H
#define _LIBQBOX_PORTS_INITIATOR_H

#include <functional>
#include <limits>
#include <cassert>
#include <cinttypes>
#include <set>
#include <utility>

#include <tlm>

#include <libqemu-cxx/libqemu-cxx.h>

#include <libgssync.h>

#include <scp/report.h>

#include <qemu-instance.h>
#include <tlm-extensions/qemu-cpu-hint.h>
#include <tlm-extensions/qemu-instance-hint.h>
#include <tlm-extensions/qemu-mr-hint.h>
#include <tlm-extensions/exclusive-access.h>
#include <tlm-extensions/shmem_extension.h>
#include <tlm-extensions/qemu-memtx-attrs.h>
#include <tlm-extensions/qemu-memtx-attr-extensions.h>
#include <tlm_sockets_buswidth.h>

class QemuInitiatorIface
{
public:
    using TlmPayload = tlm::tlm_generic_payload;

    virtual void initiator_customize_tlm_payload(TlmPayload& payload) = 0;
    virtual void initiator_tidy_tlm_payload(TlmPayload& payload) = 0;
    virtual sc_core::sc_time initiator_get_local_time() = 0;
    virtual void initiator_set_local_time(const sc_core::sc_time&) = 0;
    virtual void initiator_async_run(qemu::Cpu::AsyncJobFn job) = 0;
};

/**
 * @class QemuInitiatorSocket<>
 *
 * @brief TLM-2.0 initiator socket specialisation for QEMU AddressSpace mapping
 *
 * @details This class is used to expose a QEMU AddressSpace object as a
 *          standard TLM-2.0 initiator socket. It creates a root memory region
 *          to map the whole address space, receives I/O accesses to it and
 *          forwards them as standard TLM-2.0 transactions.
 */
template <unsigned int BUSWIDTH = DEFAULT_TLM_BUSWIDTH>
class QemuInitiatorSocket
    : public tlm::tlm_initiator_socket<BUSWIDTH, tlm::tlm_base_protocol_types, 1, sc_core::SC_ZERO_OR_MORE_BOUND>,
      public tlm::tlm_bw_transport_if<>
{
private:
    std::mutex m_mutex;
    std::vector<std::pair<sc_dt::uint64, sc_dt::uint64>> m_ranges;
    std::thread::id m_thread_id;

public:
    SCP_LOGGER(());

    using TlmInitiatorSocket = tlm::tlm_initiator_socket<BUSWIDTH, tlm::tlm_base_protocol_types, 1,
                                                         sc_core::SC_ZERO_OR_MORE_BOUND>;
    using TlmPayload = tlm::tlm_generic_payload;
    using MemTxResult = qemu::MemoryRegionOps::MemTxResult;
    using MemTxAttrs = qemu::MemoryRegionOps::MemTxAttrs;
    using DmiRegionAlias = QemuInstanceDmiManager::DmiRegionAlias;
    using DmiRegionAliasKey = uint64_t;

protected:
    QemuInstance* m_inst = nullptr;
    std::unique_ptr<QemuInstanceHintTlmExtension> m_instance_hint;
    QemuInitiatorIface* m_initiator = nullptr;
    gs::runonsysc m_on_sysc;
    int reentrancy = 0;

    std::atomic<bool> m_finished = false;

    qemu::Cpu m_cpu;
    std::shared_ptr<qemu::AddressSpace> m_as;
    std::shared_ptr<qemu::MemoryListener> m_listener;

    class m_mem_obj
    {
    public:
        std::shared_ptr<qemu::MemoryRegion> m_root;
        m_mem_obj(qemu::LibQemu& inst) { m_root.reset(new qemu::MemoryRegion(inst.object_new<qemu::MemoryRegion>())); }
        m_mem_obj(std::shared_ptr<qemu::MemoryRegion> memory): m_root(std::move(memory)) {}
    };
    m_mem_obj* m_r = nullptr;

    std::map<DmiRegionAliasKey, DmiRegionAlias::Ptr> m_dmi_aliases;
    using AliasesIterator = std::map<DmiRegionAliasKey, DmiRegionAlias::Ptr>::iterator;

    // Do not add the same target MR twice at one address.
    std::set<std::pair<uint64_t, uintptr_t>> m_qemu_mr_aliases;
    std::function<void(sc_dt::uint64, sc_dt::uint64)> m_invalidate_observer;

    void init_payload(TlmPayload& trans, tlm::tlm_command command, uint64_t addr, uint64_t* val, unsigned int size)
    {
        trans.set_command(command);
        trans.set_address(addr);
        trans.set_data_ptr(reinterpret_cast<unsigned char*>(val));
        trans.set_data_length(size);
        trans.set_streaming_width(size);
        trans.set_byte_enable_length(0);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        trans.set_extension(m_instance_hint.get());
        if (m_initiator != nullptr) m_initiator->initiator_customize_tlm_payload(trans);
        if (const auto* cpu_hint = trans.get_extension<QemuCpuHintTlmExtension>()) set_cpu(cpu_hint->get_cpu());
    }

    void add_dmi_mr_alias(DmiRegionAlias::Ptr alias)
    {
        SCP_INFO(()) << "Adding " << *alias;
        qemu::MemoryRegion alias_mr = alias->get_alias_mr();
        m_r->m_root->add_subregion(alias_mr, alias->get_start());
        alias->set_installed();
    }

    void del_dmi_mr_alias(const DmiRegionAlias::Ptr alias)
    {
        if (!alias->is_installed()) {
            return;
        }
        SCP_INFO(()) << "Removing " << *alias;
        m_r->m_root->del_subregion(alias->get_alias_mr());
    }

    tlm::tlm_dmi check_dmi_hint_locked(TlmPayload& trans)
    {
        assert(trans.is_dmi_allowed());
        tlm::tlm_dmi dmi_data;
        bool dmi_valid = false;
        const bool iothread_locked = m_inst->get().is_iothread_locked();
        if (iothread_locked) m_inst->get().unlock_iothread();
        const bool completed = m_on_sysc.run_on_sysc([&] {
            assert(m_on_sysc.is_on_sysc());
            dmi_valid = (*this)->get_direct_mem_ptr(trans, dmi_data);
        });
        if (iothread_locked) m_inst->get().lock_iothread();

        if (!completed || !dmi_valid) return dmi_data;

        int shm_fd = -1;
        if (const auto* shm_ext = trans.get_extension<gs::ShmemIDExtension>()) shm_fd = shm_ext->m_fd;

        const uint64_t start = dmi_data.get_start_address();
        const uint64_t end = dmi_data.get_end_address();
        if (start > end) return tlm::tlm_dmi{};

        const bool topology_locked = m_inst->get().is_iothread_locked();
        if (!topology_locked) m_inst->get().lock_iothread();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            constexpr size_t max_map = 250;
            if (m_dmi_aliases.size() > max_map) SCP_FATAL(())("Too many DMI regions requested");
            if (0 == m_dmi_aliases.count(start)) {
                SCP_INFO(()) << "Adding DMI for range [0x" << std::hex << dmi_data.get_start_address() << "-0x"
                             << dmi_data.get_end_address() << "]";
                qemu::RcuReadLock rcu_read_lock = m_inst->get().rcu_read_lock_new();
                DmiRegionAlias::Ptr alias = m_inst->get_dmi_manager().get_new_region_alias(dmi_data, shm_fd);
                m_dmi_aliases[start] = alias;
                add_dmi_mr_alias(m_dmi_aliases[start]);
            } else {
                SCP_INFO(())("Already have DMI for 0x{:x}", start);
            }
        }
        if (!topology_locked) m_inst->get().unlock_iothread();
        return dmi_data;
    }

    bool check_qemu_mr_hint(TlmPayload& trans)
    {
        QemuMrHintTlmExtension* ext = nullptr;
        uint64_t mapping_addr;

        trans.get_extension(ext);

        if (ext == nullptr) {
            return false;
        }

        qemu::MemoryRegion target_mr(ext->get_mr());

        if (m_r == nullptr || !target_mr.same_inst_as(*m_r->m_root)) {
            return false;
        }

        if (ext->get_offset() > trans.get_address()) return false;
        mapping_addr = trans.get_address() - ext->get_offset();

        const uint64_t target_size = target_mr.get_size();

        const uintptr_t target_mr_id = reinterpret_cast<uintptr_t>(target_mr.get_qemu_obj());
        const bool topology_locked = m_inst->get().is_iothread_locked();
        if (!topology_locked) m_inst->get().lock_iothread();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_qemu_mr_aliases.emplace(mapping_addr, target_mr_id).second) {
                auto mr = m_inst->get().template object_new<qemu::MemoryRegion>();
                mr.init_alias(*m_r->m_root, "mr-alias", target_mr, 0, target_size);
                m_r->m_root->add_subregion(mr, mapping_addr);
            }
        }
        if (!topology_locked) m_inst->get().unlock_iothread();
        return true;
    }

    void do_regular_access(TlmPayload& trans)
    {
        using sc_core::sc_time;

        uint64_t addr = trans.get_address();
        sc_time now = m_initiator != nullptr ? m_initiator->initiator_get_local_time() : sc_core::sc_time_stamp();

        m_inst->get().unlock_iothread();
        m_on_sysc.run_on_sysc([this, &trans, &now] { (*this)->b_transport(trans, now); });
        m_inst->get().lock_iothread();
        /*
         * Reset transaction address before dmi check (could be altered by
         * b_transport).
         */
        trans.set_address(addr);
        if (!check_qemu_mr_hint(trans)) {
            if (trans.is_dmi_allowed()) check_dmi_hint_locked(trans);
        }

        if (m_initiator != nullptr) m_initiator->initiator_set_local_time(now);
    }

    void do_debug_access(TlmPayload& trans)
    {
        m_inst->get().unlock_iothread();
        m_on_sysc.run_on_sysc([this, &trans] { (*this)->transport_dbg(trans); });
        m_inst->get().lock_iothread();
    }

    void do_direct_access(TlmPayload& trans)
    {
        sc_core::sc_time now = m_initiator != nullptr ? m_initiator->initiator_get_local_time()
                                                      : sc_core::sc_time_stamp();
        (*this)->b_transport(trans, now);
    }

    MemTxResult qemu_io_access(tlm::tlm_command command, uint64_t addr, uint64_t* val, unsigned int size,
                               MemTxAttrs attrs)
    {
        TlmPayload trans;
        if (m_finished || m_inst == nullptr) return qemu::MemoryRegionOps::MemTxError;

        init_payload(trans, command, addr, val, size);
        QemuMemTxAttrExtensions attr_exts(trans, attrs);

        if (trans.get_extension<ExclusiveAccessTlmExtension>()) {
            /* in the case of an exclusive access keep the iolock (and assume NO side-effects)
             * clearly dangerous, but exclusives are not guaranteed to work on IO space anyway
             */
            do_direct_access(trans);
        } else {
            std::unique_lock<std::recursive_mutex> io_lock(m_inst->g_rec_qemu_io_lock, std::try_to_lock);
            if (!io_lock.owns_lock() && !is_on_sysc()) {
                /* Allow only a single access, but handle re-entrant code,
                 * while allowing side-effects in SystemC (e.g. calling wait)
                 * [NB re-entrant code caused via memory listeners to
                 * creation of memory regions (due to DMI) in some models]
                 */
                m_inst->get().unlock_iothread();
                io_lock.lock();
                m_inst->get().lock_iothread();
            }
            reentrancy++;

            /* Force re-entrant code to use a direct access (safe for reentrancy with no side effects) */
            if (reentrancy > 1) {
                do_direct_access(trans);
            } else if (attrs.debug) {
                do_debug_access(trans);
            } else {
                do_regular_access(trans);
            }

            reentrancy--;
        }
        trans.clear_extension(m_instance_hint.get());
        if (m_initiator != nullptr) m_initiator->initiator_tidy_tlm_payload(trans);

        switch (trans.get_response_status()) {
        case tlm::TLM_OK_RESPONSE:
            return qemu::MemoryRegionOps::MemTxOK;
        case tlm::TLM_ADDRESS_ERROR_RESPONSE:
            return qemu::MemoryRegionOps::MemTxDecodeError;
        default:
            return qemu::MemoryRegionOps::MemTxError;
        }
    }

public:
    MemTxResult qemu_io_read(uint64_t addr, uint64_t* val, unsigned int size, MemTxAttrs attrs)
    {
        return qemu_io_access(tlm::TLM_READ_COMMAND, addr, val, size, attrs);
    }

    MemTxResult qemu_io_write(uint64_t addr, uint64_t val, unsigned int size, MemTxAttrs attrs)
    {
        return qemu_io_access(tlm::TLM_WRITE_COMMAND, addr, &val, size, attrs);
    }

    qemu::MemoryRegionOpsPtr create_io_ops()
    {
        using namespace std::placeholders;

        assert(m_inst);
        qemu::LibQemu& inst = m_inst->get();
        qemu::MemoryRegionOpsPtr ops = inst.memory_region_ops_new();
        ops->set_read_callback(std::bind(&QemuInitiatorSocket::qemu_io_read, this, _1, _2, _3, _4));
        ops->set_write_callback(std::bind(&QemuInitiatorSocket::qemu_io_write, this, _1, _2, _3, _4));
        ops->set_max_access_size(8);
        return ops;
    }

    bool is_on_sysc() const { return std::this_thread::get_id() == m_thread_id; }

    QemuInitiatorSocket(const char* name, QemuInstance* inst = nullptr, QemuInitiatorIface* initiator = nullptr)
        : TlmInitiatorSocket(name)
        , m_thread_id(std::this_thread::get_id())
        , m_inst(inst)
        , m_initiator(initiator)
        , m_on_sysc(sc_core::sc_gen_unique_name("initiator_run_on_sysc"))
    {
        SCP_DEBUG(()) << "QemuInitiatorSocket constructor";
        TlmInitiatorSocket::bind(*static_cast<tlm::tlm_bw_transport_if<>*>(this));
        if (inst != nullptr) m_instance_hint = std::make_unique<QemuInstanceHintTlmExtension>(*inst);
    }

    QemuInitiatorSocket(const char* name, QemuInitiatorIface& initiator, QemuInstance& inst)
        : QemuInitiatorSocket(name, &inst, &initiator)
    {
    }

    void set_cpu(qemu::Cpu cpu)
    {
        if (!m_cpu.valid()) m_cpu = cpu;
    }

    void set_invalidate_observer(std::function<void(sc_dt::uint64, sc_dt::uint64)> observer)
    {
        m_invalidate_observer = std::move(observer);
    }

    void init(qemu::Device& dev, const char* prop)
    {
        qemu::LibQemu& inst = m_inst->get();
        m_r = new m_mem_obj(inst);
        qemu::MemoryRegionOpsPtr ops = create_io_ops();
        m_r->m_root->init_io(dev, TlmInitiatorSocket::name(), std::numeric_limits<uint64_t>::max(), ops);
        dev.set_prop_link(prop, *m_r->m_root);
    }

    void init(qemu::IOMMUMemoryRegion& region)
    {
        assert(m_inst);
        assert(m_r == nullptr);
        assert(!m_as);
        assert(m_cpu.valid());
        m_r = new m_mem_obj(std::make_shared<qemu::MemoryRegion>(region.m_root_io));
        m_as = region.m_as_io;
    }

    qemu::AddressSpace& get_address_space()
    {
        assert(m_as);
        return *m_as;
    }

    void end_of_simulation() { m_finished = true; }

    ~QemuInitiatorSocket()
    {
#if 0
    // This could happen during void end_of_simulation() but there is a race with other units trying
    // to pull down their DMI's
        if (m_r) {
            if (m_r->m_root) {
                m_r->m_root->removeSubRegions();
            }
            delete m_r;
            m_r = nullptr;
        }
#endif
        //        dmimgr_unlock();
    }

    void qemu_map(qemu::MemoryListener& listener, uint64_t addr, uint64_t len)
    {
        // this function is relatively expensive, and called a lot, it should be done a different way and removed.
        if (m_finished) return;

        SCP_DEBUG(()) << "Mapping request for address [0x" << std::hex << addr << "-0x" << addr + len - 1 << "]";

        TlmPayload trans;
        uint64_t current_addr = addr;
        uint64_t temp;
        init_payload(trans, tlm::TLM_IGNORE_COMMAND, current_addr, &temp, 0);
        trans.set_dmi_allowed(true);

        while (current_addr < addr + len) {
            tlm::tlm_dmi dmi_data = check_dmi_hint_locked(trans);

            // Current addr is an absolute address while the dmi range might be relative
            // hence not necesseraly current_addr falls withing dmi_range address boundaries
            // TODO: is there a way to retrieve the dmi range block offset?
            SCP_INFO(()) << "0x" << std::hex << current_addr << " mapped [0x" << dmi_data.get_start_address() << "-0x"
                         << dmi_data.get_end_address() << "]";

            // The allocated range may not span the whole length required for mapping
            assert(dmi_data.get_end_address() >= current_addr);
            current_addr = dmi_data.get_end_address();
            if (current_addr >= addr + len) break; // Catch potential loop-rounds
            current_addr += 1;
            trans.set_address(current_addr);
        }

        trans.clear_extension(m_instance_hint.get());
        if (m_initiator != nullptr) m_initiator->initiator_tidy_tlm_payload(trans);
    }

    void init_global(qemu::Device& dev)
    {
        using namespace std::placeholders;

        qemu::LibQemu& inst = m_inst->get();
        qemu::MemoryRegionOpsPtr ops = create_io_ops();
        auto system_memory = inst.get_system_memory();
        system_memory->init_io(dev, TlmInitiatorSocket::name(), std::numeric_limits<uint64_t>::max() - 1, ops);
        m_r = new m_mem_obj(std::move(system_memory));

        m_as = inst.address_space_get_system_memory();
        // System memory has been changed from container to "io", this is relevant
        // for flatview, and to reflect that we can just update the topology
        m_as->update_topology();

        /* Sometimes memory regions are added to the global address space by (for instance) virt devices rempaiing
         * system memory such that they can directly access it. The mapped memory region needs it's DMI pointers
         * adjusted. qemu_map will do that remapping of the DMI.
         */
        m_listener = inst.memory_listener_new();
        m_listener->set_map_callback(std::bind(&QemuInitiatorSocket::qemu_map, this, _1, _2, _3));
        m_listener->register_as(m_as);
    }

    /* tlm::tlm_bw_transport_if<> */
    virtual tlm::tlm_sync_enum nb_transport_bw(tlm::tlm_generic_payload& trans, tlm::tlm_phase& phase,
                                               sc_core::sc_time& t)
    {
        /* Should not be reached */
        assert(false);
        return tlm::TLM_COMPLETED;
    }

    virtual AliasesIterator remove_alias(AliasesIterator it)
    {
        DmiRegionAlias::Ptr r = it->second;
        del_dmi_mr_alias(r);
        return m_dmi_aliases.erase(it);
    }

private:
    void invalidate_single_range(sc_dt::uint64 start_range, sc_dt::uint64 end_range)
    {
        auto it = m_dmi_aliases.upper_bound(start_range);

        if (it != m_dmi_aliases.begin()) {
            /*
             * Start with the preceding region, as it may already cross the
             * range we must invalidate.
             */
            it--;
        }
        while (it != m_dmi_aliases.end()) {
            DmiRegionAlias::Ptr r = it->second;

            if (r->get_start() > end_range) {
                /* We've got out of the invalidation range */
                break;
            }

            if (r->get_end() < start_range) {
                /* We are not in yet */
                it++;
                continue;
            }

            it = remove_alias(it);

            SCP_DEBUG(()) << "Invalidated region [0x" << std::hex << r->get_start() << ", 0x" << std::hex
                          << r->get_end() << "]";
        }
    }

    void invalidate_ranges_safe_cb()
    {
        if (m_finished) return;
        std::lock_guard<std::mutex> lock(m_mutex);

        SCP_DEBUG(()) << "Invalidating " << m_ranges.size() << " ranges";
        auto rit = m_ranges.begin();
        while (rit != m_ranges.end()) {
            invalidate_single_range(rit->first, rit->second);
            rit = m_ranges.erase(rit);
        }
    }

public:
    virtual void invalidate_direct_mem_ptr(sc_dt::uint64 start_range, sc_dt::uint64 end_range)
    {
        if (m_finished) return;
        if (m_invalidate_observer) m_invalidate_observer(start_range, end_range);
        SCP_DEBUG(()) << "DMI invalidate [0x" << std::hex << start_range << ", 0x" << std::hex << end_range << "]";
        if (!m_inst || !m_cpu.valid()) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ranges.push_back(std::make_pair(start_range, end_range));
        m_cpu.async_run([this] { invalidate_ranges_safe_cb(); });
    }

    virtual void reset()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_dmi_aliases.begin();
        while (it != m_dmi_aliases.end()) {
            it = remove_alias(it);
        }
    }
};

#endif
