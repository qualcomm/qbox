/*
 * This file is part of libqemu-cxx
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2015-2019
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <set>
#include <vector>
#include <map>
#include <atomic>

#include <libqemu-cxx/target_info.h>
#include <libqemu-cxx/exceptions.h>
#include <dynlib_loader.h>

/* Brings in the generated LibQemuExports struct and all plugin types
 * (qemu_plugin_id_t, qemu_plugin_tb, qemu_plugin_cb_flags, callbacks, ...)
 */
#include <libqemu/libqemu.h>

#include <scp/report.h>

/* libqemu types forward declaration */
struct LibQemuExports;
struct QemuObject;
struct ObjectClass;
struct DisplayGLCtxOps;
struct DisplayGLCtx;
struct QemuConsole;
struct DisplaySurface;
struct DisplayOptions;
struct QEMUCursor;
struct SDL_Window;
struct sdl2_console;
struct DisplayChangeListenerOps;
struct DisplayChangeListener;
struct MemTxAttrs;
struct QemuMemoryRegion;
struct QemuMemoryRegionOps;
struct QemuIOMMUMemoryRegion;
struct QemuAddressSpace;
struct QemuMemoryListener;
struct QemuTimer;
struct FWCfgState;
typedef void* QEMUGLContext;
struct QEMUGLParams;
typedef void (*LibQemuGfxUpdateFn)(DisplayChangeListener*, int, int, int, int);
typedef void (*LibQemuGfxSwitchFn)(DisplayChangeListener*, DisplaySurface*);
typedef void (*LibQemuRefreshFn)(DisplayChangeListener*);
typedef void (*LibQemuWindowCreateFn)(DisplayChangeListener*);
typedef void (*LibQemuWindowDestroyFn)(DisplayChangeListener*);
typedef void (*LibQemuWindowResizeFn)(DisplayChangeListener*);
typedef void (*LibQemuPollEventsFn)(DisplayChangeListener*);
typedef void (*LibQemuMouseSetFn)(DisplayChangeListener*, int, int, int);
typedef void (*LibQemuCursorDefineFn)(DisplayChangeListener*, QEMUCursor*);
typedef void (*LibQemuGLScanoutDisableFn)(DisplayChangeListener*);
typedef void (*LibQemuGLScanoutTextureFn)(DisplayChangeListener*, uint32_t, bool, uint32_t, uint32_t, uint32_t,
                                          uint32_t, uint32_t, uint32_t);
typedef void (*LibQemuGLUpdateFn)(DisplayChangeListener*, uint32_t, uint32_t, uint32_t, uint32_t);

typedef bool (*LibQemuIsCompatibleDclFn)(DisplayGLCtx*, DisplayChangeListener*);

namespace qemu {

class LibQemuInternals;
class Object;
class MemoryRegion;
class MemoryRegionOps;
class IOMMUMemoryRegion;
class AddressSpace;
class MemoryListener;
class Gpio;
class Timer;
class Bus;
class Chardev;
class DisplayOptions;
class DisplayGLCtxOps;
class Console;
class SDL2Console;
class Dcl;
class DclOps;
class RcuReadLock;

class LibQemu
{
private:
    std::shared_ptr<LibQemuInternals> m_int;
    LibraryLoaderIface& m_library_loader;
    const char* m_lib_path;
    Target m_target;
    bool m_auto_start;

    std::vector<char*> m_qemu_argv;

    LibraryLoaderIface::LibraryIfacePtr m_lib;

    QemuObject* object_new_unparented(const char* type_name);
    QemuObject* object_new_internal(const char* type_name, const char* id);

    void check_cast(Object& o, const char* type);

    void init_callbacks();

    void rcu_read_lock();
    void rcu_read_unlock();

public:
    LibQemu(LibraryLoaderIface& library_loader, const char* lib_path);
    LibQemu(LibraryLoaderIface& library_loader, Target t);
    ~LibQemu();

    /* TCG plugin API: returns the libqemu-exports table. */
    const LibQemuExports& plugin_api() const;

    void push_qemu_arg(const char* arg);
    void push_qemu_arg(std::initializer_list<const char*> args);
    const std::vector<char*>& get_qemu_args() const { return m_qemu_argv; }

    void init();
    bool is_inited() const { return m_lib != nullptr; }

    /* QEMU GDB stub
     * @port: port the gdb server will be listening on. (ex: "tcp::1234") */
    void start_gdb_server(std::string port);
    void vm_start();
    void vm_stop_paused();

    void lock_iothread();
    void unlock_iothread();

    RcuReadLock rcu_read_lock_new();

    void finish_qemu_init();
    Bus sysbus_get_default();

    void coroutine_yield();

    void system_reset();

    template <class T>
    T object_new()
    {
        T o(Object(object_new_internal(T::TYPE, NULL), m_int));
        check_cast(o, T::TYPE);

        return o;
    }

    template <class T>
    T object_new_unparented()
    {
        T o(Object(object_new_unparented(T::TYPE), m_int));
        check_cast(o, T::TYPE);

        return o;
    }
    int64_t get_virtual_clock();

    Object object_new(const char* type_name, const char* id);
    std::shared_ptr<MemoryRegionOps> memory_region_ops_new();
    std::shared_ptr<AddressSpace> address_space_new();
    std::shared_ptr<AddressSpace> address_space_get_system_memory();
    std::shared_ptr<MemoryRegion> get_system_memory();
    std::shared_ptr<MemoryListener> memory_listener_new();
    Gpio gpio_new();

    std::shared_ptr<Timer> timer_new();

    Chardev chardev_new(const char* label, const char* type);

    void tb_invalidate_phys_range(uint64_t start, uint64_t end);

    void enable_opengl();
    DisplayOptions display_options_new();
    std::vector<Console> get_all_consoles();
    Console console_lookup_by_index(int index);
    DisplayGLCtxOps display_gl_ctx_ops_new(LibQemuIsCompatibleDclFn);
    Dcl dcl_new(DisplayChangeListener* dcl);
    DclOps dcl_ops_new();

    int sdl2_init() const;
    const char* sdl2_get_error() const;

    std::vector<SDL2Console> sdl2_create_consoles(int num);
    void sdl2_cleanup();
    void sdl2_2d_update(DisplayChangeListener* dcl, int x, int y, int w, int h);
    void sdl2_2d_switch(DisplayChangeListener* dcl, DisplaySurface* new_surface);
    void sdl2_2d_refresh(DisplayChangeListener* dcl);

    void sdl2_gl_update(DisplayChangeListener* dcl, int x, int y, int w, int h);
    void sdl2_gl_switch(DisplayChangeListener* dcl, DisplaySurface* new_surface);
    void sdl2_gl_refresh(DisplayChangeListener* dcl);

    void sdl2_window_create(DisplayChangeListener* dcl);
    void sdl2_window_destroy(DisplayChangeListener* dcl);
    void sdl2_window_resize(DisplayChangeListener* dcl);
    void sdl2_poll_events(DisplayChangeListener* dcl);

    void dcl_dpy_gfx_replace_surface(DisplayChangeListener* dcl, DisplaySurface* new_surface);

    QEMUGLContext sdl2_gl_create_context(DisplayGLCtx* dgc, QEMUGLParams* p);
    void sdl2_gl_destroy_context(DisplayGLCtx* dgc, QEMUGLContext gl_ctx);
    int sdl2_gl_make_context_current(DisplayGLCtx* dgc, QEMUGLContext gl_ctx);

    bool virgl_has_blob() const;

    FWCfgState* fw_cfg_find();
    void fw_cfg_set_dma_as(FWCfgState* s);
    void fw_cfg_add_i16(FWCfgState* s, uint16_t key, uint16_t value);
    void fw_cfg_add_file(FWCfgState* s, const char* filename, void* data, size_t len);
    void* fw_cfg_modify_file(FWCfgState* s, const char* filename, void* data, size_t len);
};

class RcuReadLock
{
private:
    std::shared_ptr<LibQemuInternals> m_int;
    RcuReadLock() = default;

public:
    RcuReadLock(std::shared_ptr<LibQemuInternals> internals);
    ~RcuReadLock();

    RcuReadLock(const RcuReadLock&) = delete;
    RcuReadLock& operator=(const RcuReadLock&) = delete;
    RcuReadLock(RcuReadLock&&);
    RcuReadLock& operator=(RcuReadLock&&);
};

class Object
{
protected:
    QemuObject* m_obj = nullptr;
    std::shared_ptr<LibQemuInternals> m_int;

    bool check_cast_by_type(const char* type_name) const { return true; }

public:
    Object() = default;
    Object(QemuObject* obj, std::shared_ptr<LibQemuInternals>& internals);
    Object(const Object& o);
    Object(Object&& o);

    Object& operator=(Object o);

    virtual ~Object();

    bool valid() const { return m_obj != nullptr; }

    void set_prop_bool(const char* name, bool val);
    void set_prop_int(const char* name, int64_t val);
    void set_prop_uint(const char* name, uint64_t val);
    void set_prop_str(const char* name, const char* val);
    void set_prop_link(const char* name, const Object& link);
    void set_prop_parse(const char* name, const char* value);

    Object get_prop_link(const char* name);

    QemuObject* get_qemu_obj() const { return m_obj; }

    LibQemu& get_inst();
    uintptr_t get_inst_id() const { return reinterpret_cast<uintptr_t>(m_int.get()); }
    bool same_inst_as(const Object& o) const { return get_inst_id() == o.get_inst_id(); }

    template <class T>
    bool check_cast() const
    {
        return check_cast_by_type(T::TYPE);
    }

    void clear_callbacks();
};

class Gpio : public Object
{
public:
    typedef std::function<void(bool)> GpioEventFn;

    class GpioProxy
    {
    protected:
        bool m_prev_valid = false;
        bool m_prev;
        GpioEventFn m_cb;
        std::atomic<bool> m_cb_set{ false };

    public:
        void event(bool level)
        {
            if (!m_prev_valid || (level != m_prev)) {
                if (m_cb_set.load(std::memory_order_acquire)) m_cb(level);
            }

            m_prev_valid = true;
            m_prev = level;
        }

        void set_callback(GpioEventFn cb)
        {
            if (cb) {
                m_cb = std::move(cb);
                m_cb_set.store(true, std::memory_order_release);
            } else {
                m_cb_set.store(false, std::memory_order_release);
            }
        }
    };

private:
    std::shared_ptr<GpioProxy> m_proxy;

public:
    static constexpr const char* const TYPE = "irq";

    Gpio() = default;
    Gpio(const Gpio& o) = default;
    Gpio(const Object& o): Object(o) {}

    void set(bool lvl);

    void set_proxy(std::shared_ptr<GpioProxy> proxy) { m_proxy = proxy; }

    void set_event_callback(GpioEventFn cb)
    {
        if (m_proxy) {
            m_proxy->set_callback(cb);
        }
    }
};

class MemoryRegionOps
{
public:
    enum MemTxResult { MemTxOK, MemTxError, MemTxDecodeError, MemTxOKExitTB };

    struct MemTxAttrs {
        bool secure = false;
        bool debug = false;

        MemTxAttrs() = default;
        MemTxAttrs(const ::MemTxAttrs& qemu_attrs);
    };

    typedef std::function<MemTxResult(uint64_t, uint64_t*, unsigned int, MemTxAttrs)> ReadCallback;

    typedef std::function<MemTxResult(uint64_t, uint64_t, unsigned int, MemTxAttrs)> WriteCallback;

private:
    QemuMemoryRegionOps* m_ops;
    std::shared_ptr<LibQemuInternals> m_int;

    ReadCallback m_read_cb;
    WriteCallback m_write_cb;

public:
    MemoryRegionOps(QemuMemoryRegionOps* ops, std::shared_ptr<LibQemuInternals> internals);
    ~MemoryRegionOps();

    void set_read_callback(ReadCallback cb);
    void set_write_callback(WriteCallback cb);

    void set_max_access_size(unsigned size);

    ReadCallback get_read_callback() { return m_read_cb; }
    WriteCallback get_write_callback() { return m_write_cb; }

    QemuMemoryRegionOps* get_qemu_mr_ops() { return m_ops; }
};

typedef std::shared_ptr<MemoryRegionOps> MemoryRegionOpsPtr;

class MemoryRegion : public Object
{
private:
    MemoryRegionOpsPtr m_ops;
    std::set<MemoryRegion> m_subregions;
    int m_priority = 0;

    void internal_del_subregion(const MemoryRegion& mr);

public:
    using MemTxResult = MemoryRegionOps::MemTxResult;
    using MemTxAttrs = MemoryRegionOps::MemTxAttrs;

    static constexpr const char* const TYPE = "memory-region";

    MemoryRegion* container = nullptr;

    MemoryRegion() = default;
    MemoryRegion(const MemoryRegion&) = default;
    MemoryRegion(const Object& o): Object(o) {}
    MemoryRegion(QemuMemoryRegion* mr, std::shared_ptr<LibQemuInternals> internals);

    ~MemoryRegion();
    void removeSubRegions();

    uint64_t get_size();
    int get_priority() const { return m_priority; }
    /* Make sure to set the priority before calling `add_subregion_overlap()` */
    void set_priority(int priority) { m_priority = priority; }

    void init(const Object& owner, const char* name, uint64_t size);
    void init_io(Object owner, const char* name, uint64_t size, MemoryRegionOpsPtr ops);
    void init_ram_ptr(Object owner, const char* name, uint64_t size, void* ptr, int fd = -1);
    void init_alias(Object owner, const char* name, const MemoryRegion& root, uint64_t offset, uint64_t size);

    void add_subregion(MemoryRegion& mr, uint64_t offset);
    void add_subregion_overlap(MemoryRegion& mr, uint64_t offset);
    void del_subregion(const MemoryRegion& mr);

    MemTxResult dispatch_read(uint64_t addr, uint64_t* data, uint64_t size, MemTxAttrs attrs);

    MemTxResult dispatch_write(uint64_t addr, uint64_t data, uint64_t size, MemTxAttrs attrs);

    void set_ops(const MemoryRegionOpsPtr ops);

    bool operator<(const MemoryRegion& mr) const { return m_obj < mr.m_obj; }
};

class AddressSpace
{
private:
    QemuAddressSpace* m_as;
    std::shared_ptr<LibQemuInternals> m_int;

    bool m_inited = false;
    bool m_global = false;

public:
    using MemTxResult = MemoryRegionOps::MemTxResult;
    using MemTxAttrs = MemoryRegionOps::MemTxAttrs;

    AddressSpace(QemuAddressSpace* as, std::shared_ptr<LibQemuInternals> internals);
    AddressSpace(const AddressSpace&) = delete;

    ~AddressSpace();

    QemuAddressSpace* get_ptr() { return m_as; }

    void init(MemoryRegion mr, const char* name, bool global = false);

    MemTxResult read(uint64_t addr, void* data, size_t size, MemTxAttrs attrs);
    MemTxResult write(uint64_t addr, const void* data, size_t size, MemTxAttrs attrs);

    void update_topology();
};

/* this is simply used to hold shared_ptr's for memory management and
 * to check if the DMIRegions exist.
 */
struct DmiRegionBase {
};
class IOMMUMemoryRegion : public MemoryRegion
{
public:
    static constexpr const char* const TYPE = "libqemu-iommu-memory-region";

    qemu::MemoryRegion m_root_te;
    std::shared_ptr<qemu::AddressSpace> m_as_te;
    qemu::MemoryRegion m_root_io;
    std::shared_ptr<qemu::AddressSpace> m_as_io;
    std::map<uint64_t, std::shared_ptr<DmiRegionBase>> m_dmi_aliases_te;
    std::map<uint64_t, std::shared_ptr<DmiRegionBase>> m_dmi_aliases_io;
    uint64_t min_page_sz;

    IOMMUMemoryRegion(const Object& o)
        : MemoryRegion(o)
        , m_root_te(get_inst().object_new_unparented<qemu::MemoryRegion>())
        , m_as_te(get_inst().address_space_new())
        , m_root_io(get_inst().object_new_unparented<qemu::MemoryRegion>())
        , m_as_io(get_inst().address_space_new())
    {
    }

    typedef enum {
        IOMMU_NONE = 0,
        IOMMU_RO = 1,
        IOMMU_WO = 2,
        IOMMU_RW = 3,
    } IOMMUAccessFlags;

    struct IOMMUTLBEntry {
        QemuAddressSpace* target_as;
        uint64_t iova;
        uint64_t translated_addr;
        uint64_t addr_mask;
        IOMMUAccessFlags perm;
    };

    std::map<uint64_t, qemu::IOMMUMemoryRegion::IOMMUTLBEntry> m_mapped_te;

    using IOMMUTranslateCallbackFn = std::function<void(IOMMUTLBEntry*, uint64_t, IOMMUAccessFlags, int)>;
    void init(const Object& owner, const char* name, uint64_t size, MemoryRegionOpsPtr ops,
              IOMMUTranslateCallbackFn cb);
    void iommu_unmap(IOMMUTLBEntry*);
};

class MemoryListener
{
public:
    typedef std::function<void(MemoryListener&, uint64_t addr, uint64_t len)> MapCallback;

private:
    QemuMemoryListener* m_ml;
    std::shared_ptr<LibQemuInternals> m_int;
    std::shared_ptr<AddressSpace> m_as;

    MapCallback m_map_cb;

public:
    MemoryListener(std::shared_ptr<LibQemuInternals> internals);
    MemoryListener(const MemoryListener&) = delete;
    ~MemoryListener();

    void set_ml(QemuMemoryListener* ml);

    void set_map_callback(MapCallback cb);
    MapCallback& get_map_callback() { return m_map_cb; }

    void register_as(std::shared_ptr<AddressSpace> as);
};

class DisplayOptions
{
public:
    ::DisplayOptions* m_opts;
    std::shared_ptr<LibQemuInternals> m_int;

    DisplayOptions() = default;
    DisplayOptions(::DisplayOptions* opts, std::shared_ptr<LibQemuInternals>& internals);
    DisplayOptions(const DisplayOptions&) = default;
};

class DisplayGLCtxOps
{
public:
    ::DisplayGLCtxOps* m_ops;
    std::shared_ptr<LibQemuInternals> m_int;

    DisplayGLCtxOps() = default;
    DisplayGLCtxOps(::DisplayGLCtxOps* ops, std::shared_ptr<LibQemuInternals>& internals);
    DisplayGLCtxOps(const DisplayGLCtxOps&) = default;
};

class Console
{
public:
    QemuConsole* m_cons;
    std::shared_ptr<LibQemuInternals> m_int;

    Console() = default;
    Console(QemuConsole* cons, std::shared_ptr<LibQemuInternals>& internals);
    Console(const Console&) = default;

    int get_index() const;
    bool is_graphic() const;
    void set_display_gl_ctx(DisplayGLCtx*);
    void set_window_id(int id);
};

class SDL2Console
{
public:
    struct sdl2_console* m_cons;
    std::shared_ptr<LibQemuInternals> m_int;

    SDL2Console() = default;
    SDL2Console(struct sdl2_console* cons, std::shared_ptr<LibQemuInternals>& internals);
    SDL2Console(const SDL2Console&) = default;

    void init(Console& con, void* user_data);
    void set_hidden(bool hidden);
    void set_idx(int idx);
    void set_opts(DisplayOptions& opts);
    void set_opengl(bool opengl);
    void set_dcl_ops(DclOps& dcl_ops);
    void set_dgc_ops(DisplayGLCtxOps& dgc_ops);

    SDL_Window* get_real_window() const;
    DisplayChangeListener* get_dcl() const;
    DisplayGLCtx* get_dgc() const;

    void register_dcl() const;

    void set_window_id(Console& con) const;
};

class Dcl
{
private:
    DisplayChangeListener* m_dcl;
    std::shared_ptr<LibQemuInternals> m_int;

public:
    Dcl(DisplayChangeListener* dcl, std::shared_ptr<LibQemuInternals>& internals);

    void* get_user_data();
};

class DclOps
{
public:
    DisplayChangeListenerOps* m_ops;
    std::shared_ptr<LibQemuInternals> m_int;

    DclOps() = default;
    DclOps(DisplayChangeListenerOps* ops, std::shared_ptr<LibQemuInternals>& internals);
    DclOps(const DclOps&) = default;

    void set_name(const char* name);
    bool is_used_by(DisplayChangeListener* dcl) const;
    void set_gfx_update(LibQemuGfxUpdateFn gfx_update_fn);
    void set_gfx_switch(LibQemuGfxSwitchFn gfx_switch_fn);
    void set_refresh(LibQemuRefreshFn refresh_fn);

    void set_window_create(LibQemuWindowCreateFn window_create_fn);
    void set_window_destroy(LibQemuWindowDestroyFn window_destroy_fn);
    void set_window_resize(LibQemuWindowResizeFn window_resize_fn);
    void set_poll_events(LibQemuPollEventsFn poll_events_fn);
};

class Device : public Object
{
public:
    static constexpr const char* const TYPE = "device";

    Device() = default;
    Device(const Device&) = default;
    Device(const Object& o): Object(o) {}

    void connect_gpio_out(int idx, Gpio gpio);
    void connect_gpio_out_named(const char* name, int idx, Gpio gpio);

    Gpio get_gpio_in(int idx);
    Gpio get_gpio_in_named(const char* name, int idx);

    Bus get_child_bus(const char* name);
    void set_parent_bus(Bus bus);

    void set_prop_chardev(const char* name, Chardev chr);
    void set_prop_uint_array(const char* name, std::vector<unsigned int> vec);
};

class SysBusDevice : public Device
{
public:
    static constexpr const char* const TYPE = "sys-bus-device";

    SysBusDevice() = default;
    SysBusDevice(const SysBusDevice&) = default;
    SysBusDevice(const Object& o): Device(o) {}

    MemoryRegion mmio_get_region(int id);

    void connect_gpio_out(int idx, Gpio gpio);
};

class GpexHost : public SysBusDevice
{
public:
    static constexpr const char* const TYPE = "gpex-pcihost";

    GpexHost() = default;
    GpexHost(const GpexHost&) = default;
    GpexHost(const Object& o): SysBusDevice(o) {}

    void set_irq_num(int idx, int gic_irq);
};

class Cpu : public Device
{
public:
    static constexpr const char* const TYPE = "cpu";

    using EndOfLoopCallbackFn = std::function<void()>;
    using CpuKickCallbackFn = std::function<void()>;
    using AsyncJobFn = std::function<void()>;

    Cpu() = default;
    Cpu(const Cpu&) = default;
    Cpu(const Object& o): Device(o) {}

    int get_index() const;

    void loop();
    bool loop_is_busy();

    bool can_run();

    void set_soft_stopped(bool stopped);
    void halt(bool halted);

    void reset(bool reset); /* Reset: on true, enter reset, call cpu_pause and cpu_reset
                             * on false exit reset and call cpu_resume */

    void set_unplug(bool unplug);
    void remove_sync();

    void register_thread();

    Cpu set_as_current();

    void kick();

    [[noreturn]] void exit_loop_from_io();

    void async_run(AsyncJobFn job);
    void async_safe_run(AsyncJobFn job);

    void set_end_of_loop_callback(EndOfLoopCallbackFn cb);
    void set_kick_callback(CpuKickCallbackFn cb);

    bool is_in_exclusive_context() const;

    void set_vcpu_dirty(bool dirty) const;
};

class Timer
{
public:
    typedef std::function<void()> TimerCallbackFn;

private:
    std::shared_ptr<LibQemuInternals> m_int;
    QemuTimer* m_timer = nullptr;
    TimerCallbackFn m_cb;

public:
    Timer(std::shared_ptr<LibQemuInternals> internals);
    ~Timer();

    void set_callback(TimerCallbackFn cb);

    void mod(int64_t deadline);
    void del();
};

class Bus : public Object
{
public:
    static constexpr const char* const TYPE = "bus";

    Bus() = default;
    Bus(const Bus& o) = default;
    Bus(const Object& o): Object(o) {}
};

class Chardev : public Object
{
public:
    static constexpr const char* const TYPE = "chardev";

    Chardev() = default;
    Chardev(const Chardev& o) = default;
    Chardev(const Object& o): Object(o) {}
};

}; /* namespace qemu */
