/*
 * This file is part of libqemu-cxx
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2015-2019
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <libqemu/libqemu.h>

#include <libqemu-cxx/libqemu-cxx.h>
#include <internals.h>

namespace qemu {

int Cpu::get_index() const { return m_int->exports().cpu_get_index(m_obj); }

void Cpu::loop() { m_int->exports().cpu_loop(m_obj); }

bool Cpu::loop_is_busy() { return m_int->exports().cpu_loop_is_busy(m_obj); }

bool Cpu::can_run() { return m_int->exports().cpu_can_run(m_obj); }

void Cpu::set_soft_stopped(bool stopped) { m_int->exports().cpu_set_soft_stopped(m_obj, stopped); }

void Cpu::halt(bool halted) { m_int->exports().cpu_halt(m_obj, halted); }

void Cpu::reset(bool reset) { m_int->exports().cpu_reset(m_obj, reset); }

void Cpu::set_unplug(bool unplug) { m_int->exports().cpu_set_unplug(m_obj, unplug); }

void Cpu::remove_sync() { m_int->exports().cpu_remove_sync(m_obj); }

void Cpu::register_thread() { m_int->exports().cpu_register_thread(m_obj); }

Cpu Cpu::set_as_current()
{
    Cpu ret(Object(m_int->exports().current_cpu_get(), m_int));

    m_int->exports().current_cpu_set(m_obj);

    return ret;
}

void Cpu::kick() { m_int->exports().cpu_kick(m_obj); }

static void generic_async_run(void* opaque)
{
    Cpu::AsyncJobFn* job = reinterpret_cast<Cpu::AsyncJobFn*>(opaque);

    (*job)();

    delete job;
}

void Cpu::async_run(AsyncJobFn job)
{
    AsyncJobFn* dyn_job = new AsyncJobFn(job);
    m_int->exports().async_run_on_cpu(m_obj, &generic_async_run, dyn_job);
}

void Cpu::async_safe_run(AsyncJobFn job)
{
    AsyncJobFn* dyn_job = new AsyncJobFn(job);
    m_int->exports().async_safe_run_on_cpu(m_obj, &generic_async_run, dyn_job);
}

[[noreturn]] void Cpu::exit_loop_from_io()
{
    uintptr_t pc = m_int->exports().cpu_get_mem_io_pc(m_obj);

    auto cpu_loop_exit_noexc = m_int->exports().cpu_loop_exit_noexc;

    m_int->exports().cpu_restore_state(m_obj, pc);
    cpu_loop_exit_noexc(m_obj);

    /*
     * Use the function abort() to convince GCC we actually won't return.
     * We have to do this because the function pointer type in the export
     * does not has the noreturn attribute but the qemu function is really
     * "noreturn"
     */
    std::abort();
}

void Cpu::set_end_of_loop_callback(Cpu::EndOfLoopCallbackFn cb)
{
    m_int->get_cpu_end_of_loop_cb().register_cb(*this, cb);
}

void Cpu::set_kick_callback(Cpu::CpuKickCallbackFn cb) { m_int->get_cpu_kick_cb().register_cb(*this, cb); }

bool Cpu::is_in_exclusive_context() const { return m_int->exports().cpu_in_exclusive_context(m_obj); }

void Cpu::set_vcpu_dirty(bool dirty) const { m_int->exports().cpu_set_vcpu_dirty(m_obj, dirty); }

}; // namespace qemu
