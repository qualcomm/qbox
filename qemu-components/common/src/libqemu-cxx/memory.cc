/*
 * This file is part of libqemu-cxx
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2015-2019
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cassert>

#include <libqemu/libqemu.h>

#include <libqemu-cxx/libqemu-cxx.h>
#include <internals.h>
#include <limits>
#include <math.h>

namespace qemu {

/* ::MemTxtResult <-> MemoryRegionOps::MemTxResult mapping */
static inline MemoryRegionOps::MemTxResult QEMU_TO_LIB_MEMTXRESULT_MAPPING(uint32_t value)
{
    switch (value) {
    case MEMTX_OK:
        return MemoryRegionOps::MemTxOK;
    case MEMTX_ERROR:
        return MemoryRegionOps::MemTxError;
    case MEMTX_DECODE_ERROR:
        return MemoryRegionOps::MemTxDecodeError;
    case MEMTX_OK_EXIT_TB:
        return MemoryRegionOps::MemTxOKExitTB;
    }
    return MemoryRegionOps::MemTxError;
}

static inline uint32_t LIB_TO_QEMU_MEMTXRESULT_MAPPING(MemoryRegionOps::MemTxResult value)
{
    switch (value) {
    case MemoryRegionOps::MemTxOK:
        return MEMTX_OK;
    case MemoryRegionOps::MemTxError:
        return MEMTX_ERROR;
    case MemoryRegionOps::MemTxDecodeError:
        return MEMTX_DECODE_ERROR;
    case MemoryRegionOps::MemTxOKExitTB:
        return MEMTX_OK_EXIT_TB;
    }
    return MEMTX_ERROR;
}

/*
 * ===============
 * MemoryRegionOps
 * ===============
 */

MemoryRegionOps::MemTxAttrs::MemTxAttrs(const ::MemTxAttrs& qemu_attrs)
    : secure(qemu_attrs.secure)
    , space(qemu_attrs.space)
    , user(qemu_attrs.user)
    , memory(qemu_attrs.memory)
    , debug(qemu_attrs.debug)
    , requester_id(qemu_attrs.requester_id)
    , pid(qemu_attrs.pid)
    , address_type(qemu_attrs.address_type)
    , unspecified(qemu_attrs.unspecified)
{
}

static ::MemTxAttrs to_qemu_memtx_attrs(MemoryRegionOps::MemTxAttrs attrs)
{
    ::MemTxAttrs qemu_attrs = {};
    qemu_attrs.secure = attrs.secure;
    qemu_attrs.space = attrs.space;
    qemu_attrs.user = attrs.user;
    qemu_attrs.memory = attrs.memory;
    qemu_attrs.debug = attrs.debug;
    qemu_attrs.requester_id = attrs.requester_id;
    qemu_attrs.pid = attrs.pid;
    qemu_attrs.address_type = attrs.address_type;
    qemu_attrs.unspecified = attrs.unspecified;
    return qemu_attrs;
}

MemoryRegionOps::MemoryRegionOps(QemuMemoryRegionOps* ops, std::shared_ptr<LibQemuInternals> internals)
    : m_ops(ops), m_int(internals)
{
}

MemoryRegionOps::~MemoryRegionOps() { m_int->exports().mr_ops_free(m_ops); }

static ::MemTxResult generic_read_cb(void* opaque, hwaddr addr, uint64_t* data, unsigned int size,
                                     MemTxAttrs qemu_attrs)
{
    MemoryRegionOps* ops = reinterpret_cast<MemoryRegionOps*>(opaque);
    MemoryRegionOps::MemTxAttrs attrs(qemu_attrs);
    MemoryRegionOps::MemTxResult res;

    res = ops->get_read_callback()(addr, data, size, attrs);
    return LIB_TO_QEMU_MEMTXRESULT_MAPPING(res);
}

void MemoryRegionOps::set_read_callback(ReadCallback cb)
{
    m_read_cb = cb;
    m_int->exports().mr_ops_set_read_cb(m_ops, generic_read_cb);
}

static ::MemTxResult generic_write_cb(void* opaque, hwaddr addr, uint64_t data, unsigned int size,
                                      MemTxAttrs qemu_attrs)
{
    MemoryRegionOps* ops = reinterpret_cast<MemoryRegionOps*>(opaque);
    MemoryRegionOps::MemTxAttrs attrs(qemu_attrs);
    MemoryRegionOps::MemTxResult res;

    res = ops->get_write_callback()(addr, data, size, attrs);
    return LIB_TO_QEMU_MEMTXRESULT_MAPPING(res);
}

void MemoryRegionOps::set_write_callback(WriteCallback cb)
{
    m_write_cb = cb;
    m_int->exports().mr_ops_set_write_cb(m_ops, generic_write_cb);
}

void MemoryRegionOps::set_max_access_size(unsigned size) { m_int->exports().mr_ops_set_max_access_size(m_ops, size); }

/*
 * ============
 * MemoryRegion
 * ============
 */
MemoryRegion::MemoryRegion(QemuMemoryRegion* mr, std::shared_ptr<LibQemuInternals> internals)
    : Object(reinterpret_cast<QemuObject*>(mr), internals)
{
}

/*  perform sub region removal */
void MemoryRegion::removeSubRegions()
{
    for (auto& mr : m_subregions) {
        if (mr.container) internal_del_subregion(mr);
        // Dont attempt to re-delete these, when we clear the vector below
        const_cast<MemoryRegion*>(&mr)->m_obj = nullptr;
    }
    m_subregions.clear();
}

MemoryRegion::~MemoryRegion()
{
    for (auto& mr : m_subregions) {
        internal_del_subregion(mr);
    }

    m_subregions.clear();
}

uint64_t MemoryRegion::get_size()
{
    QemuMemoryRegion* mr = reinterpret_cast<QemuMemoryRegion*>(m_obj);
    return m_int->exports().memory_region_size(mr);
}

void MemoryRegion::init(const Object& owner, const char* name, uint64_t size)
{
    QemuMemoryRegion* mr = reinterpret_cast<QemuMemoryRegion*>(m_obj);
    m_int->exports().memory_region_init(mr, owner.get_qemu_obj(), name, size);
}

void MemoryRegion::init_io(Object owner, const char* name, uint64_t size, MemoryRegionOpsPtr ops)
{
    QemuMemoryRegion* mr = reinterpret_cast<QemuMemoryRegion*>(m_obj);
    QemuMemoryRegionOps* qemu_ops = ops->get_qemu_mr_ops();

    m_ops = ops;

    m_int->exports().memory_region_init_io(mr, owner.get_qemu_obj(), qemu_ops, m_ops.get(), name, size);
}

void MemoryRegion::init_ram_ptr(Object owner, const char* name, uint64_t size, void* ptr, int fd)
{
    QemuMemoryRegion* mr = reinterpret_cast<QemuMemoryRegion*>(m_obj);

    m_int->exports().memory_region_init_ram_ptr(mr, owner.get_qemu_obj(), name, size, ptr);
    m_int->exports().libqemu_memory_region_set_fd(mr, fd);
}

void MemoryRegion::init_alias(Object owner, const char* name, const MemoryRegion& root, uint64_t offset, uint64_t size)
{
    QemuMemoryRegion* mr = reinterpret_cast<QemuMemoryRegion*>(m_obj);
    QemuMemoryRegion* root_mr = reinterpret_cast<QemuMemoryRegion*>(root.m_obj);

    m_int->exports().memory_region_init_alias(mr, owner.get_qemu_obj(), name, root_mr, offset, size);
}

void MemoryRegion::add_subregion(MemoryRegion& mr, uint64_t offset)
{
    QemuMemoryRegion* this_mr = reinterpret_cast<QemuMemoryRegion*>(m_obj);
    QemuMemoryRegion* sub_mr = reinterpret_cast<QemuMemoryRegion*>(mr.m_obj);

    assert(mr.m_priority == 0 && "For priorities use add_subregion_overlap()");
    m_int->exports().memory_region_add_subregion(this_mr, offset, sub_mr);
    m_subregions.insert(mr);
    mr.container = this;
}

void MemoryRegion::add_subregion_overlap(MemoryRegion& mr, uint64_t offset)
{
    QemuMemoryRegion* this_mr = reinterpret_cast<QemuMemoryRegion*>(m_obj);
    QemuMemoryRegion* sub_mr = reinterpret_cast<QemuMemoryRegion*>(mr.m_obj);

    m_int->exports().memory_region_add_subregion_overlap(this_mr, offset, sub_mr, mr.m_priority);
    m_subregions.insert(mr);
    mr.container = this;
}

void MemoryRegion::internal_del_subregion(const MemoryRegion& mr)
{
    QemuMemoryRegion* this_mr = reinterpret_cast<QemuMemoryRegion*>(m_obj);
    QemuMemoryRegion* sub_mr = reinterpret_cast<QemuMemoryRegion*>(mr.m_obj);

    m_int->exports().memory_region_del_subregion(this_mr, sub_mr);
}

void MemoryRegion::del_subregion(const MemoryRegion& mr)
{
    internal_del_subregion(mr);
    m_subregions.erase(mr);
}

MemoryRegion::MemTxResult MemoryRegion::dispatch_read(uint64_t addr, uint64_t* data, uint64_t size, MemTxAttrs attrs)
{
    ::MemTxResult qemu_res;
    QemuMemoryRegion* mr = reinterpret_cast<QemuMemoryRegion*>(m_obj);

    qemu_res = m_int->exports().memory_region_dispatch_read(mr, addr, data, size, to_qemu_memtx_attrs(attrs));

    return QEMU_TO_LIB_MEMTXRESULT_MAPPING(qemu_res);
}

MemoryRegion::MemTxResult MemoryRegion::dispatch_write(uint64_t addr, uint64_t data, uint64_t size, MemTxAttrs attrs)
{
    ::MemTxResult qemu_res;
    QemuMemoryRegion* mr = reinterpret_cast<QemuMemoryRegion*>(m_obj);

    qemu_res = m_int->exports().memory_region_dispatch_write(mr, addr, data, size, to_qemu_memtx_attrs(attrs));

    return QEMU_TO_LIB_MEMTXRESULT_MAPPING(qemu_res);
}

void IOMMUMemoryRegion::init(const Object& owner, const char* name, uint64_t size, MemoryRegionOpsPtr ops,
                             IOMMUTranslateCallbackFn cb, IOMMUAttrsToIndexCallbackFn attrs_to_index_cb,
                             IOMMUNumIndexesCallbackFn num_indexes_cb, IOMMUIndexToAttrsCallbackFn index_to_attrs_cb)
{
    m_root_io.init_io(*this, "IOMMU untranslated root", std::numeric_limits<uint64_t>::max(), ops);
    m_as_io->init(m_root_io, "IOMMU untranslated AddressSpace");

    QemuIOMMUMemoryRegion* mr = reinterpret_cast<QemuIOMMUMemoryRegion*>(m_obj);
    typedef std::function<int(uint64_t, uint64_t*, unsigned int, MemTxAttrs)> LibQemuIOMMUTranslateCallBack;

    m_int->exports().iommu_memory_region_init(mr, owner.get_qemu_obj(), name, size);

    m_int->get_iommu_translate_cb().register_cb(*this, cb);
    if (attrs_to_index_cb) {
        m_int->get_iommu_attrs_to_index_cb().register_cb(
            *this, [attrs_to_index_cb](int* index, MemTxAttrs attrs) { *index = attrs_to_index_cb(attrs); });
    }
    if (num_indexes_cb) {
        m_int->get_iommu_num_indexes_cb().register_cb(*this, [num_indexes_cb](int* count) { *count = num_indexes_cb(); });
    }
    m_index_to_attrs_cb = std::move(index_to_attrs_cb);

    auto s = m_int->exports().memory_region_iommu_get_min_page_size(mr);
    min_page_sz = log2(s);
}

void IOMMUMemoryRegion::iommu_unmap(IOMMUTLBEntry* te)
{
    QemuIOMMUMemoryRegion* mr = reinterpret_cast<QemuIOMMUMemoryRegion*>(m_obj);
    m_int->exports().iommu_unmap(mr, (QemuIOMMUTLBEntry*)te);
}

AddressSpace::AddressSpace(QemuAddressSpace* as, std::shared_ptr<LibQemuInternals> internals)
    : m_as(as), m_int(internals)
{
}

AddressSpace::~AddressSpace()
{
    if (m_global) {
        return;
    }
    if (m_inited) {
        m_int->exports().address_space_destroy(m_as);
    }
}

void AddressSpace::init(MemoryRegion mr, const char* name, bool global)
{
    QemuMemoryRegion* mr_obj = reinterpret_cast<QemuMemoryRegion*>(mr.get_qemu_obj());
    m_int->exports().address_space_init(m_as, mr_obj, name);

    if (global) {
        m_global = true;
    }
    m_inited = true;
}

AddressSpace::MemTxResult AddressSpace::read(uint64_t addr, void* data, size_t size, AddressSpace::MemTxAttrs attrs)
{
    ::MemTxResult qemu_res;

    qemu_res = m_int->exports().address_space_read(m_as, addr, to_qemu_memtx_attrs(attrs), data, size);

    return QEMU_TO_LIB_MEMTXRESULT_MAPPING(qemu_res);
}

AddressSpace::MemTxResult AddressSpace::write(uint64_t addr, const void* data, size_t size,
                                              AddressSpace::MemTxAttrs attrs)
{
    ::MemTxResult qemu_res;

    qemu_res = m_int->exports().address_space_write(m_as, addr, to_qemu_memtx_attrs(attrs), data, size);

    return QEMU_TO_LIB_MEMTXRESULT_MAPPING(qemu_res);
}

void AddressSpace::update_topology() { m_int->exports().address_space_update_topology(m_as); }

MemoryListener::MemoryListener(std::shared_ptr<LibQemuInternals> internals): m_ml{ nullptr }, m_int(internals) {}

MemoryListener::~MemoryListener()
{
    if (m_ml) {
        m_int->exports().memory_listener_free(m_ml);
    }
}

void MemoryListener::set_ml(QemuMemoryListener* ml)
{
    assert(!m_ml && "Qemu memory listener already set");
    m_ml = ml;
}

static void generic_map_cb(void* opaque, hwaddr addr, hwaddr len)
{
    auto* ml = reinterpret_cast<MemoryListener*>(opaque);
    ml->get_map_callback()(*ml, addr, len);
}

void MemoryListener::set_map_callback(MapCallback cb)
{
    m_map_cb = cb;
    m_int->exports().memory_listener_set_map_cb(m_ml, generic_map_cb);
}

void MemoryListener::register_as(std::shared_ptr<AddressSpace> as)
{
    assert(m_ml && as->get_ptr());
    m_as = as;
    m_int->exports().memory_listener_register(m_ml, as->get_ptr());
}

}; // namespace qemu
