/*
 * This file is part of libqbox
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <systemc>

#include <display.h>

MainThreadQemuDisplay::~MainThreadQemuDisplay()
{
    /*
     * Prevent display callbacks from performing BQL unlock/lock operations
     * during teardown.  On macOS, sdl2_cleanup() calls
     * SDL_QuitSubSystem(SDL_INIT_VIDEO) which can trigger window close
     * events that flow through QEMU's display system and invoke our
     * callbacks.  Without this, the callbacks would call
     * unlock_iothread() while the BQL is not held, hitting the
     * bql_locked() assertion.
     */
    m_simulation_started = false;

    m_ops.set_name("deleted");
    m_ops.set_gfx_switch(nullptr);
    m_ops.set_gfx_update(nullptr);
    m_ops.set_refresh(nullptr);
    m_ops.set_window_create(nullptr);
    m_ops.set_window_destroy(nullptr);
    m_ops.set_window_resize(nullptr);
    m_ops.set_poll_events(nullptr);
    inst->get().sdl2_cleanup();
}

MainThreadQemuDisplay::MainThreadQemuDisplay(QemuDevice* gpu)
{
    if (!gpu) {
        SCP_FATAL() << "Ivalid GPU";
    }
    QemuInstance* gpu_qemu_inst = &gpu->get_qemu_inst();
    if (inst && inst != gpu_qemu_inst) {
        SCP_FATAL() << "Can not create another MainThreadQemuDisplay on a different QemuInstance";
    }
    MainThreadQemuDisplay::inst = gpu_qemu_inst;
}

MainThreadQemuDisplay::MainThreadQemuDisplay(sc_core::sc_object* o): MainThreadQemuDisplay(selectGpu(o)) {}

MainThreadQemuDisplay::MainThreadQemuDisplay(QemuVirtioGpu& gpu)
    : MainThreadQemuDisplay(reinterpret_cast<QemuDevice*>(&gpu))
{
}

MainThreadQemuDisplay::MainThreadQemuDisplay(QemuVirtioMMIOGpuGl& gpu)
    : MainThreadQemuDisplay(reinterpret_cast<QemuDevice*>(&gpu))
{
}

QemuDevice* MainThreadQemuDisplay::selectGpu(sc_core::sc_object* o)
{
    if (dynamic_cast<QemuVirtioMMIOGpuGl*>(o) != nullptr) {
        return dynamic_cast<QemuVirtioMMIOGpuGl*>(o);
    } else if (dynamic_cast<QemuVirtioGpu*>(o) != nullptr) {
        return dynamic_cast<QemuVirtioGpu*>(o);
    } else if (dynamic_cast<ramfb*>(o) != nullptr) {
        return dynamic_cast<ramfb*>(o);
    } else {
        SCP_FATAL() << "The type of the object 'o' is not a QemuVirtioMMIOGpuGl or QemuVirtioGpu";
        return nullptr;
    }
}

void MainThreadQemuDisplay::instantiate()
{
    if (m_instantiated) {
        return;
    }

    inst->get().enable_opengl();

    if (inst->get().sdl2_init() < 0) {
        SCP_WARN() << "Skipping Display module: Failed to initialize SDL: " << inst->get().sdl2_get_error();
        return;
    }

    m_instantiated = true;
}

void MainThreadQemuDisplay::gl_switch(DisplayChangeListener* dcl, DisplaySurface* new_surface)
{
    qemu::LibQemu& lib = inst->get();
    MainThreadQemuDisplay* display = reinterpret_cast<MainThreadQemuDisplay*>(lib.dcl_new(dcl).get_user_data());

    // Let's switch only after simulation is started, and make sure we are NOT on SystemC
    // thread as GL API should be called from QEMU's thread.
    if (display->m_simulation_started && !display->m_on_sysc.is_on_sysc()) {
        lib.sdl2_gl_switch(dcl, new_surface);
    }
}

// SDL2 window functions should run on main kernel thread as it is a MacOS requirement. At
// initialization time, it is scheduled to run on SystemC kernel thread without waiting for
// its completion. This will prevent the deadlocking situation where SystemC is not running
// the switch "on_sysc" as it is waiting for QEMU to "finish/yeld", but that does not happen
// as QEMU is in turn waiting for its switch function to run on SystemC kernel thread.
void MainThreadQemuDisplay::window_create(DisplayChangeListener* dcl)
{
    qemu::LibQemu& lib = inst->get();
    MainThreadQemuDisplay* display = reinterpret_cast<MainThreadQemuDisplay*>(lib.dcl_new(dcl).get_user_data());

    /*
     * GL switch continues immediately with context and texture creation, so
     * the macOS window must exist before the callback returns. During
     * elaboration, keep the callback asynchronous to avoid the startup
     * deadlock; once simulation is running, release BQL while synchronously
     * rendezvousing with the SystemC thread.
     */
    if (display->m_simulation_started) {
        lib.unlock_iothread();
    }

    display->m_on_sysc.run_on_sysc([&lib, dcl]() { lib.sdl2_window_create(dcl); }, display->m_simulation_started);

    if (display->m_simulation_started) {
        lib.lock_iothread();
    }
}

void MainThreadQemuDisplay::window_destroy(DisplayChangeListener* dcl)
{
    qemu::LibQemu& lib = inst->get();
    MainThreadQemuDisplay* display = reinterpret_cast<MainThreadQemuDisplay*>(lib.dcl_new(dcl).get_user_data());
    // SDL2 window destroy should run on main-thread
    display->m_on_sysc.fork_on_systemc([&lib, dcl]() { lib.sdl2_window_destroy(dcl); });
}

void MainThreadQemuDisplay::window_resize(DisplayChangeListener* dcl)
{
    qemu::LibQemu& lib = inst->get();
    MainThreadQemuDisplay* display = reinterpret_cast<MainThreadQemuDisplay*>(lib.dcl_new(dcl).get_user_data());
    // SDL2 window resize should run on main-thread
    display->m_on_sysc.fork_on_systemc([&lib, dcl]() { lib.sdl2_window_resize(dcl); });
}

void MainThreadQemuDisplay::poll_events(DisplayChangeListener* dcl)
{
    qemu::LibQemu& lib = inst->get();
    MainThreadQemuDisplay* display = reinterpret_cast<MainThreadQemuDisplay*>(lib.dcl_new(dcl).get_user_data());
    // SDL2 poll events should run on main-thread
    display->m_on_sysc.fork_on_systemc([&lib, dcl]() { lib.sdl2_poll_events(dcl); });
}

bool MainThreadQemuDisplay::is_compatible_dcl(DisplayGLCtx* ctx, DisplayChangeListener* dcl)
{
    MainThreadQemuDisplay* display = reinterpret_cast<MainThreadQemuDisplay*>(inst->get().dcl_new(dcl).get_user_data());
    return display->m_ops.is_used_by(dcl);
}

void MainThreadQemuDisplay::realize()
{
    using namespace std::placeholders;

    if (!m_instantiated || m_realized) {
        return;
    }

    qemu::LibQemu& lib = inst->get();

    // We do not call sdl2_display_init. Instead, we set our own display
    // callbacks which would run on the SystemC thread.

    std::vector<qemu::Console> consoles = lib.get_all_consoles();
    if (consoles.empty()) {
        SCP_FATAL() << "Failed to get any console. Please make sure graphics device is "
                       "realized before the display.";
    }

    qemu::DisplayOptions dpy_opts = lib.display_options_new();

    m_sdl2_consoles = lib.sdl2_create_consoles(consoles.size());

    for (long unsigned int i = 0; i < consoles.size(); ++i) {
        qemu::Console& cons = consoles[i];
        qemu::SDL2Console& sdl2_console = m_sdl2_consoles[i];
        sdl2_console.init(cons, this);

        if (!cons.is_graphic() && cons.get_index() != 0) {
            sdl2_console.set_hidden(i != 0);
        }
        sdl2_console.set_idx(i);
        sdl2_console.set_opengl(true);
        sdl2_console.set_opts(dpy_opts);

        m_ops = lib.dcl_ops_new();
        m_ops.set_name("sc");
        // Register our own callback to make sure it's called only when simulation starts
        m_ops.set_gfx_switch(&gl_switch);
        m_ops.set_window_create(&window_create);
        m_ops.set_window_destroy(&window_destroy);
        m_ops.set_window_resize(&window_resize);
        m_ops.set_poll_events(&poll_events);
        sdl2_console.set_dcl_ops(m_ops);

        m_gl_ctx_ops = lib.display_gl_ctx_ops_new(&is_compatible_dcl);
        sdl2_console.set_dgc_ops(m_gl_ctx_ops);

        cons.set_display_gl_ctx(sdl2_console.get_dgc());
        sdl2_console.register_dcl();

        sdl2_console.set_window_id(cons);
    }

    m_realized = true;
}

display::display(const sc_core::sc_module_name& _name, sc_core::sc_object* o)
    : sc_module(_name)
#ifdef __APPLE__
    // On MacOS use libqbox's display SystemC module.
    , m_main_display(o)
#endif
{
#ifndef __APPLE__
    // Use QEMU's integrated display only if we are NOT on MacOS.
    // On MacOS use libqbox's display SystemC module.
    QemuDevice* gpu = (dynamic_cast<QemuDevice*>(o));
    gpu->get_qemu_inst().set_display_arg("sdl,gl=on");
#endif
}

display::display(const sc_core::sc_module_name& name, QemuDevice& gpu)
    : display(name, reinterpret_cast<sc_core::sc_object*>(&gpu))
{
}

display::display(const sc_core::sc_module_name& name, QemuVirtioGpu& gpu)
    : display(name, reinterpret_cast<sc_core::sc_object*>(&gpu))
{
}

display::display(const sc_core::sc_module_name& name, QemuVirtioMMIOGpuGl& gpu)
    : display(name, reinterpret_cast<sc_core::sc_object*>(&gpu))
{
}

void display::before_end_of_elaboration()
{
#ifdef __APPLE__
    m_main_display.instantiate();
#endif
}

void display::end_of_elaboration()
{
#ifdef __APPLE__
    m_main_display.realize();
#endif
}

void display::start_of_simulation()
{
#ifdef __APPLE__
    m_main_display.start_of_simulation();
#endif
}

QemuInstance* display::get_qemu_inst()
{
#ifdef __APPLE__
    return &m_main_display.get_qemu_inst();
#else
    return nullptr;
#endif
}

bool display::is_instantiated() const
{
#ifdef __APPLE__
    return m_main_display.is_instantiated();
#else
    return true;
#endif
}

bool display::is_realized() const
{
#ifdef __APPLE__
    return m_main_display.is_realized();
#else
    return true;
#endif
}

const std::vector<qemu::SDL2Console>* display::get_sdl2_consoles() const
{
#ifdef __APPLE__
    return &m_main_display.get_sdl2_consoles();
#else
    return nullptr;
#endif
}

void module_register() { GSC_MODULE_REGISTER_C(display, sc_core::sc_object*); }
