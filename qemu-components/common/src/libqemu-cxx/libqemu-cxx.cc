/*
 * This file is part of libqemu-cxx
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2015-2019
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstring>
#include <cassert>

#include <libqemu/libqemu.h>

#include <libqemu-cxx/libqemu-cxx.h>
#include <internals.h>

namespace qemu {

LibQemu::LibQemu(LibraryLoaderIface& library_loader, const char* lib_path)
    : m_library_loader(library_loader), m_lib_path(lib_path), m_auto_start(true)
{
}

LibQemu::LibQemu(LibraryLoaderIface& library_loader, Target t)
    : m_library_loader(library_loader), m_lib_path(nullptr), m_target(t), m_auto_start(true)
{
}

LibQemu::~LibQemu()
{
    for (auto s : m_qemu_argv) {
        delete[] s;
    }
}

void LibQemu::push_qemu_arg(const char* arg)
{
    char* dst = new char[std::strlen(arg) + 1];
    std::strcpy(dst, arg);
    if (std::strncmp(arg, "-S", 2) == 0) {
        m_auto_start = false;
    }

    assert(!is_inited());
    m_qemu_argv.push_back(dst);
}

void LibQemu::push_qemu_arg(std::initializer_list<const char*> args)
{
    for (auto arg : args) {
        push_qemu_arg(arg);
    }
}

void LibQemu::init()
{
    const char* libname;
    LibQemuInitFct qemu_init = nullptr;
    LibQemuExports* exports = nullptr;

    if (m_lib_path == nullptr) {
        /* constructed with a Target */
        libname = get_target_lib(m_target);

        if (libname == nullptr) {
            /*
             * Null libname means libqemu hasn't been compiled with support
             * for this target.
             */
            throw TargetNotSupportedException(m_target);
        }
    } else {
        /* constructed with a lib path */
        libname = m_lib_path;
    }

    m_lib = m_library_loader.load_library(libname, true);

    if (m_lib == nullptr) {
        throw LibraryLoadErrorException(libname, m_library_loader.get_last_error());
    }

    if (!m_lib->symbol_exists(LIBQEMU_INIT_SYM_STR)) {
        throw InvalidLibraryException(libname, LIBQEMU_INIT_SYM_STR);
    }

    qemu_init = reinterpret_cast<LibQemuInitFct>(m_lib->get_symbol(LIBQEMU_INIT_SYM_STR));
    exports = qemu_init(m_qemu_argv.size(), &m_qemu_argv[0]);

    m_int = std::make_shared<LibQemuInternals>(*this, exports);
    init_callbacks();
}

const LibQemuExports& LibQemu::plugin_api() const { return m_int->exports(); }

void LibQemu::start_gdb_server(std::string port)
{
    m_int->exports().gdbserver_start(port.c_str());
    m_int->exports().libqemu_set_autostart(m_auto_start);
}

void LibQemu::vm_start() { m_int->exports().vm_start(); }

void LibQemu::vm_stop_paused() { m_int->exports().vm_stop_paused(); }

int64_t LibQemu::get_virtual_clock() { return m_int->exports().clock_virtual_get_ns(); }

void LibQemu::tb_invalidate_phys_range(uint64_t start, uint64_t end)
{
    m_int->exports().tb_invalidate_phys_range(start, end);
}
QemuObject* LibQemu::object_new_unparented(const char* type_name) { return m_int->exports().object_new(type_name); }
QemuObject* LibQemu::object_new_internal(const char* type_name, const char* id)
{
    QemuObject* o = object_new_unparented(type_name);
    QemuObject* root = m_int->exports().object_get_root();

    char name[20]; // helpful for debugging.
    snprintf(name, sizeof(name), "qbox-%.10s[*]", type_name);
    m_int->exports().object_property_add_child(root, name, o);

    if (id && (strlen(id))) {
        m_int->exports().qdev_set_id(reinterpret_cast<QemuDevice*>(o), id);
    }

    return o;
}

Object LibQemu::object_new(const char* type_name, const char* id)
{
    Object o(object_new_internal(type_name, id), m_int);
    return o;
}

std::shared_ptr<MemoryRegionOps> LibQemu::memory_region_ops_new()
{
    QemuMemoryRegionOps* ops = m_int->exports().mr_ops_new();

    return std::make_shared<MemoryRegionOps>(ops, m_int);
}

std::shared_ptr<AddressSpace> LibQemu::address_space_new()
{
    QemuAddressSpace* as = m_int->exports().address_space_new();

    return std::make_shared<AddressSpace>(as, m_int);
}

std::shared_ptr<AddressSpace> LibQemu::address_space_get_system_memory()
{
    QemuAddressSpace* as = m_int->exports().address_space_get_system_memory();
    return std::make_shared<AddressSpace>(as, m_int);
}

std::shared_ptr<MemoryRegion> LibQemu::get_system_memory()
{
    QemuMemoryRegion* mr = m_int->exports().get_system_memory();
    return std::make_shared<MemoryRegion>(mr, m_int);
}

std::shared_ptr<MemoryListener> LibQemu::memory_listener_new()
{
    auto ret = std::make_shared<MemoryListener>(m_int);
    QemuMemoryListener* ml = m_int->exports().memory_listener_new(ret.get(), "QboxMemoryListener");
    ret->set_ml(ml);
    return ret;
}

static void qemu_gpio_generic_handler(void* opaque, int n, int level)
{
    Gpio::GpioProxy* proxy = reinterpret_cast<Gpio::GpioProxy*>(opaque);

    proxy->event(level);
}

Gpio LibQemu::gpio_new()
{
    std::shared_ptr<Gpio::GpioProxy> proxy = std::make_shared<Gpio::GpioProxy>();

    QemuGpio* qemu_gpio = m_int->exports().gpio_new(qemu_gpio_generic_handler, proxy.get());

    Gpio gpio(Object(reinterpret_cast<QemuObject*>(qemu_gpio), m_int));
    gpio.set_proxy(proxy);

    return gpio;
}

std::shared_ptr<Timer> LibQemu::timer_new() { return std::make_shared<Timer>(m_int); }

Chardev LibQemu::chardev_new(const char* label, const char* type)
{
    QemuChardev* qemu_char_dev = m_int->exports().char_dev_new(label, type);
    Chardev ret(Object(reinterpret_cast<QemuObject*>(qemu_char_dev), m_int));

    return ret;
}

void LibQemu::check_cast(Object& o, const char* type) { /* TODO */ }

void LibQemu::lock_iothread() { m_int->exports().qemu_mutex_lock_iothread(); }

void LibQemu::unlock_iothread() { m_int->exports().qemu_mutex_unlock_iothread(); }

void LibQemu::rcu_read_lock() { m_int->exports().rcu_read_lock(); }

void LibQemu::rcu_read_unlock() { m_int->exports().rcu_read_unlock(); }

RcuReadLock LibQemu::rcu_read_lock_new() { return RcuReadLock(m_int); }

void LibQemu::coroutine_yield() { m_int->exports().coroutine_yield(); }

void LibQemu::finish_qemu_init() { m_int->exports().finish_qemu_init(); }

void LibQemu::system_reset() { m_int->exports().system_reset(); }

Bus LibQemu::sysbus_get_default()
{
    QemuBus* qemu_bus = m_int->exports().sysbus_get_default();
    Object obj(reinterpret_cast<QemuObject*>(qemu_bus), m_int);

    return Bus(obj);
}

void LibQemu::enable_opengl() { m_int->exports().enable_opengl(); }

DisplayOptions LibQemu::display_options_new()
{
    ::DisplayOptions* opts = m_int->exports().display_options_new();
    return DisplayOptions(opts, m_int);
}

Console LibQemu::console_lookup_by_index(int index)
{
    QemuConsole* qemu_cons = m_int->exports().console_lookup_by_index(index);
    return Console(qemu_cons, m_int);
}

std::vector<Console> LibQemu::get_all_consoles()
{
    std::vector<Console> consoles;
    int i = 0;
    while (QemuConsole* qemu_cons = m_int->exports().console_lookup_by_index(i)) {
        consoles.push_back(Console(qemu_cons, m_int));
        ++i;
    }
    return consoles;
}

int LibQemu::sdl2_init() const { return m_int->exports().sdl2_init(); }

const char* LibQemu::sdl2_get_error() const { return m_int->exports().sdl2_get_error(); }

std::vector<SDL2Console> LibQemu::sdl2_create_consoles(int num)
{
    m_int->exports().sdl2_create_consoles(num);
    std::vector<SDL2Console> ret;
    for (int i = 0; i < num; ++i) {
        sdl2_console* cons = m_int->exports().sdl2_get_console(i);
        ret.push_back(SDL2Console(cons, m_int));
    }
    return ret;
}

void LibQemu::sdl2_cleanup() { m_int->exports().sdl_cleanup(); }

DisplayGLCtxOps LibQemu::display_gl_ctx_ops_new(LibQemuIsCompatibleDclFn is_compatible_dcl_fn)
{
    ::DisplayGLCtxOps* ops = m_int->exports().display_gl_ctx_ops_new(is_compatible_dcl_fn);
    return DisplayGLCtxOps(ops, m_int);
}

Dcl LibQemu::dcl_new(DisplayChangeListener* dcl)
{
    assert(dcl != nullptr);
    return Dcl(dcl, m_int);
}

DclOps LibQemu::dcl_ops_new()
{
    DisplayChangeListenerOps* ops = m_int->exports().dcl_ops_new();
    return DclOps(ops, m_int);
}

void LibQemu::sdl2_2d_update(DisplayChangeListener* dcl, int x, int y, int w, int h)
{
    m_int->exports().sdl2_2d_update(dcl, x, y, w, h);
}

void LibQemu::sdl2_2d_switch(DisplayChangeListener* dcl, DisplaySurface* new_surface)
{
    m_int->exports().sdl2_2d_switch(dcl, new_surface);
}

void LibQemu::sdl2_2d_refresh(DisplayChangeListener* dcl) { m_int->exports().sdl2_2d_refresh(dcl); }

void LibQemu::sdl2_gl_update(DisplayChangeListener* dcl, int x, int y, int w, int h)
{
    m_int->exports().sdl2_gl_update(dcl, x, y, w, h);
}

void LibQemu::sdl2_gl_switch(DisplayChangeListener* dcl, DisplaySurface* new_surface)
{
    m_int->exports().sdl2_gl_switch(dcl, new_surface);
}

void LibQemu::dcl_dpy_gfx_replace_surface(DisplayChangeListener* dcl, DisplaySurface* new_surface)
{
    m_int->exports().dcl_dpy_gfx_replace_surface(dcl, new_surface);
}

void LibQemu::sdl2_gl_refresh(DisplayChangeListener* dcl) { m_int->exports().sdl2_gl_refresh(dcl); }

void LibQemu::sdl2_window_create(DisplayChangeListener* dcl) { m_int->exports().sdl2_window_create(dcl); }

void LibQemu::sdl2_window_destroy(DisplayChangeListener* dcl) { m_int->exports().sdl2_window_destroy(dcl); }

void LibQemu::sdl2_window_resize(DisplayChangeListener* dcl) { m_int->exports().sdl2_window_resize(dcl); }

void LibQemu::sdl2_poll_events(DisplayChangeListener* dcl) { m_int->exports().sdl2_poll_events(dcl); }

QEMUGLContext LibQemu::sdl2_gl_create_context(DisplayGLCtx* dgc, QEMUGLParams* p)
{
    return m_int->exports().sdl2_gl_create_context(dgc, p);
}

void LibQemu::sdl2_gl_destroy_context(DisplayGLCtx* dgc, QEMUGLContext gl_ctx)
{
    m_int->exports().sdl2_gl_destroy_context(dgc, gl_ctx);
}

int LibQemu::sdl2_gl_make_context_current(DisplayGLCtx* dgc, QEMUGLContext gl_ctx)
{
    return m_int->exports().sdl2_gl_make_context_current(dgc, gl_ctx);
}

bool LibQemu::virgl_has_blob() const { return m_int->exports().virgl_has_blob(); }

FWCfgState* LibQemu::fw_cfg_find() { return m_int->exports().fw_cfg_find(); }

void LibQemu::fw_cfg_set_dma_as(FWCfgState* s)
{
    auto as = address_space_get_system_memory();
    return m_int->exports().fw_cfg_set_dma_as(s, as->get_ptr());
}

void LibQemu::fw_cfg_add_i16(FWCfgState* s, uint16_t key, uint16_t value)
{
    return m_int->exports().fw_cfg_add_i16(s, key, value);
}

void LibQemu::fw_cfg_add_file(FWCfgState* s, const char* filename, void* data, size_t len)
{
    return m_int->exports().fw_cfg_add_file(s, filename, data, len);
}

void* LibQemu::fw_cfg_modify_file(FWCfgState* s, const char* filename, void* data, size_t len)
{
    return m_int->exports().fw_cfg_modify_file(s, filename, data, len);
}

} // namespace qemu
