/*
 * This file is part of libqemu-cxx
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _LIBQEMU_CXX_INTERNALS_
#define _LIBQEMU_CXX_INTERNALS_

#include <atomic>
#include <map>
#include <memory>

#include <libqemu/libqemu.h>
#include <libqemu-cxx/libqemu-cxx.h>
#include <libqemu-cxx/target/riscv.h>

namespace qemu {

class LibQemuObjectCallbackBase
{
public:
    virtual void clear(Object obj) = 0;
};

template <typename T>
class LibQemuObjectCallback : public LibQemuObjectCallbackBase
{
private:
    struct Entry {
        T cb;
        std::atomic<bool> active{ true };
    };
    std::map<QemuObject*, std::unique_ptr<Entry>> m_cbs;

public:
    void register_cb(Object obj, T cb)
    {
        auto entry = std::make_unique<Entry>();
        entry->cb = std::move(cb);
        entry->active.store(true, std::memory_order_release);
        m_cbs[obj.get_qemu_obj()] = std::move(entry);
    }

    void clear(Object obj) override
    {
        auto it = m_cbs.find(obj.get_qemu_obj());
        if (it != m_cbs.end() && it->second) {
            it->second->active.store(false, std::memory_order_release);
        }
    }

    template <typename... Args>
    void call(QemuObject* obj, Args... args) const
    {
        auto it = m_cbs.find(obj);
        if (it == m_cbs.end() || !it->second) {
            return;
        }

        if (it->second->active.load(std::memory_order_acquire)) {
            it->second->cb(args...);
        }
    }
};

class LibQemuInternals
{
private:
    LibQemu& m_inst;
    LibQemuExports* m_exports = nullptr;

    LibQemuObjectCallback<Cpu::EndOfLoopCallbackFn> m_cpu_end_of_loop_cbs;
    LibQemuObjectCallback<Cpu::CpuKickCallbackFn> m_cpu_kick_cbs;
    LibQemuObjectCallback<IOMMUMemoryRegion::IOMMUTranslateCallbackFn> m_iommu_translate_cbs;
    LibQemuObjectCallback<CpuRiscv64::MipUpdateCallbackFn> m_riscv_mip_update_cbs;
    std::vector<std::shared_ptr<Gpio::GpioProxy>> m_gpio_proxies;

    std::vector<LibQemuObjectCallbackBase*> m_cbs{
        &m_cpu_end_of_loop_cbs,
        &m_cpu_kick_cbs,
        &m_iommu_translate_cbs,
        &m_riscv_mip_update_cbs,
    };

public:
    LibQemuInternals(LibQemu& inst, LibQemuExports* exports): m_inst(inst), m_exports(exports) {}

    const LibQemuExports& exports() const { return *m_exports; };
    LibQemu& get_inst() { return m_inst; }

    void clear_callbacks(Object obj)
    {
        for (auto cb : m_cbs) {
            cb->clear(obj);
        }
    }

    LibQemuObjectCallback<Cpu::EndOfLoopCallbackFn>& get_cpu_end_of_loop_cb() { return m_cpu_end_of_loop_cbs; }

    LibQemuObjectCallback<Cpu::CpuKickCallbackFn>& get_cpu_kick_cb() { return m_cpu_kick_cbs; }
    LibQemuObjectCallback<IOMMUMemoryRegion::IOMMUTranslateCallbackFn>& get_iommu_translate_cb()
    {
        return m_iommu_translate_cbs;
    }

    LibQemuObjectCallback<CpuRiscv64::MipUpdateCallbackFn>& get_cpu_riscv_mip_update_cb()
    {
        return m_riscv_mip_update_cbs;
    }

    void retain_gpio_proxy(std::shared_ptr<Gpio::GpioProxy> proxy) { m_gpio_proxies.push_back(std::move(proxy)); }
};

} // namespace qemu

#endif
