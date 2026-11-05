# MCIPS Plugin (Multi-Core Instructions Per Second Plugin)

The **MCIPS plugin** is a SystemC/C++ component that provides time synchronization between **QEMU** and **SystemC** by leveraging QEMU's TCG (Tiny Code Generator) plugin API. It uses instruction counts to coordinate simulation time. MCIPS is optional and is not required for QNN use.

> **Test coverage:** The configured MCIPS test variants use **TCG-MULTI**. This does not establish coverage of every runtime configuration.

***

## 1. Selecting the MCIPS Time-Sync Strategy from Lua Configuration

To use the plugin, set `time_sync_strategy = "mcips"` inside the `QemuInstance` configuration:

```lua
qemu_inst = {
    moduletype = "QemuInstance";
    args = {"&platform.qemu_inst_mgr", "AARCH64"};
    accel = "tcg",                        -- MCIPS requires TCG mode
    tcg_mode = "MULTI",                   -- Multi-threaded TCG
    sync_policy = "multithread-unconstrained",

    time_sync_strategy = "mcips",         -- Use the MCIPS plugin for time sync

    log_level = 0
}
```

The strategy accepts `"quantum_keeper"` (the default) and `"mcips"`. Setting it to `"quantum_keeper"` (or leaving it unset) uses the traditional quantum keeper synchronization mechanism instead of the MCIPS plugin.

Each CPU can be configured with its own instruction execution rate:

```lua
for i = 0, (ARM_NUM_CPUS - 1) do
    local cpu = {
        moduletype = "cpu_arm_cortexA76";
        args = {"&platform.qemu_inst"};
        mem = {bind = "&router.target_socket"};

        insn_per_second = 100000000;  -- 100 MIPS per CPU
        -- insn_per_second = ((i+1) * 100000000);  -- Different speeds per CPU
    };
    platform["cpu_"..tostring(i)] = cpu;
end
```

***

## 2. How the MCIPS Plugin Works

The MCIPS plugin is implemented as a **SystemC module** (`McipsPlugin` in `mcips-plugin.h`) that inherits from the **`LibQemuPlugin`** base class. Here is how it operates:

### **Thread Model**

| Thread | Entry points |
|---|---|
| QEMU vCPU (BQL held) | `vcpu_init`, `vcpu_idle`, `vcpu_resume`, `vcpu_tb_trans`, `vcpu_tb_exec_cond`, `cpu_end_delta_quota` |
| SystemC | `receive_window_cb`, `idle_tick_method` |
| QEMU timer | `get_qemu_clock` (no locks) |

**Lock ordering:** `BQL → m_mcips_mutex` (never reversed).

### **Lock-Free Atomics**

| Variable | Ordering | Purpose |
|---|---|---|
| `m_shutdown` | seq_cst | Shutdown flag checked by guarded QEMU callbacks and `receive_window_cb` |
| `m_inflight_cb` | seq_cst | Shutdown drain counter for `receive_window_cb` |
| `m_qemu_time_ns` | release/acquire | Nanosecond shadow of `m_qemu_time` for `get_qemu_clock` |
| `m_active_vcpu` | acquire (lock-free) / relaxed (under mutex) | Active CPU pointer |
| `m_first_vcpu_initialized` | release/acquire | Gates the startup idle pump until the first vCPU exists |

### **QEMU TCG Plugin Integration**
- **libidlinker Plugin**: When MCIPS is enabled, `QemuInstance` requests `libidlinker.dll` on Windows, `libidlinker.dylib` on macOS, and `libidlinker.so` on Linux.
- **Plugin ID Communication**: The integration path uses `global_set_cci_param()` to pass the plugin ID to QBox, which stores it in a `cci::cci_param<uint64_t>` (`m_id`). The external API implementation, fetched library, and build paths are dependency details and are not verified here.
- **Event Registration**: Every callback is registered directly via `m_inst.plugin_api().qemu_plugin_register_*_cb(...)` as a capture-less lambda that calls `dispatch_userdata()`, passing `handle_as_userdata()` as the userdata.
- **Callback Dispatch**: Each capture-less lambda decays to the C function pointer QEMU expects and routes the callback to the C++ member function via `dispatch_userdata()`

### **Instruction-Based Timing Model**
- **Instruction Quota**: Each vCPU runs a global quantum (instruction count limit) before synchronization
- **Delta Tracking**: Tracks `delta_insn` (instructions executed since last sync) per vCPU using QEMU scoreboards
- **Time Calculation**: Converts instruction count to simulation time using `insn_per_second` parameter. `qemu_time_now()` has an overload accepting the active pointer to avoid redundant atomic loads when called under `m_mcips_mutex`.
- **Active vCPU Management**: Maintains an atomic pointer (`m_active_vcpu`) to the currently active (executing) vCPU

### **Translation Block Instrumentation**
```cpp
// For each TB, the plugin:
// 1. Counts instructions inline (atomic increment)
m_inst.plugin_api().qemu_plugin_register_vcpu_tb_exec_inline_per_vcpu(
    tb, QEMU_PLUGIN_INLINE_ADD_U64, delta_insn, n_insns);

// 2. Registers conditional callback when quota reached
m_inst.plugin_api().qemu_plugin_register_vcpu_tb_exec_cond_cb(
    tb,
    [](unsigned int cpu_index, void* userdata) {
        LibQemuPlugin::dispatch_userdata(userdata, [&](LibQemuPlugin* p) {
            static_cast<McipsPlugin*>(p)->vcpu_tb_exec_cond(cpu_index, userdata);
        });
    },
    QEMU_PLUGIN_CB_NO_REGS, QEMU_PLUGIN_COND_GE, delta_insn, m_global_quantum, handle_as_userdata());
```

### **Multi-vCPU State Management**
- **vCPU States (`vCPUTimeStatus`)**: Each vCPU can be in `IDLE`, `RUNNING`, or `PAUSED` state
- **Per-vCPU data (`vCPUTime` struct)**: Tracks `index`, `insn_per_second`, `delta_insn`, `cpu_time`, `cpu_execution_status`
- **Active vCPU Selection**: `select_active_vcpu()` picks the first vCPU whose status is not `IDLE`; it does not filter out `PAUSED` vCPUs.
- **Slowest CPU Tracking**: `slowest_none_idle_cpu()` finds the CPU that is furthest behind in simulation time
- **Pause/Resume Logic**: `pause_if_ahead()` pauses vCPUs that advance too far ahead; `resume_if_behind()` resumes those that fall behind. The active CPU pointer is loaded once and threaded through both functions to minimize atomic contention.

### **Idle Time Pump**
When all CPUs are idle (e.g., all in WFI/WFE state), the plugin starts an **idle time pump** (`m_idle_tick` / `idle_tick_method()`) so that QEMU timers can still fire and move simulation time forward. `detach` alone does not advance time — the SystemC kernel only steps toward a pending timed event, and with every CPU idle nothing else produces one. The pump is the `m_idle_tick.notify(m_quantum)` itself: the scheduled event forces the kernel to step time to deliver it. It is kicked from `vcpu_idle()` when the last active CPU goes idle, and from `get_qemu_clock()` at startup (gated by `m_first_vcpu_initialized`, before any vCPU exists). `vcpu_resume()` re-attaches the window when the first CPU wakes, so QEMU retakes the clock and the pump lapses. When no CPU is active, `idle_tick_method()` does nothing after loading `m_active_vcpu` — it is a pure "sink" for the async_event, and just being scheduled is enough to advance `sc_time_stamp()`.

### **Atomic Time Copy**
`m_qemu_time_ns` is an atomic copy of `m_qemu_time` in nanoseconds. `get_qemu_clock()` runs on QEMU's timer thread without holding `m_mcips_mutex`, so it reads the base time from this shadow (release/acquire) rather than the non-atomic `sc_time m_qemu_time` — reading the latter off-mutex while vCPU threads write it under the mutex would be a torn read. It is updated by `sync_qemu_time_ns()` every time `m_qemu_time` changes while holding `m_mcips_mutex`.

### **Shutdown and Cleanup**

`McipsPlugin` implements deterministic shutdown through multiple guards:

- **`m_shutdown` (atomic bool)**: Set during shutdown; guarded entrypoints check it, while `shutdown_bridge()` rejects new QEMU callback dispatch.
- **`m_inflight_cb` (atomic int)**: Counts how many `receive_window_cb` calls are running right now; shutdown waits for this to reach zero before continuing.
- **`shutdown_bridge()`**: Called from both `end_of_simulation()` and the destructor. Safe to call more than once (the second call does nothing).
- **Detach on shutdown**: If the window is attached, QBox calls the dependency's detach operation with the current `m_qemu_time`. The dependency's internal detach behavior is outside this guide.

#### Shutdown Double-Check Pattern

`receive_window_cb` uses a double-check pattern to safely interact with
the `m_inflight_cb` counter during shutdown:

```
1. Check m_shutdown → if true, return early (no increment)
2. Increment m_inflight_cb
3. Re-check m_shutdown → if true, decrement and return
4. ... do work ...
5. Decrement m_inflight_cb
```

This prevents a race where `shutdown_cleanup()` finishes draining
`m_inflight_cb` before a concurrent `receive_window_cb` has incremented
it, which would cause a use-after-free. The second check in step 3
catches the case where shutdown started between steps 1 and 2.

***

## 3. Timing Parameters and Synchronization Accuracy

The MCIPS plugin's synchronization behavior is controlled by two critical timing parameters:

### **quantum_ns Parameter**

The `quantum_ns` parameter is the TLM global quantum in nanoseconds. The main application default is `1e6` ns (`platforms/src/main.cc`); the Dragonwing configuration sets it to `1e7` ns (`platforms/dragonwing/configs/fw/common_conf/auto_gen4/auto_gen4.lua`). These are current code and configuration defaults, not recommendations or verified optimal values.

### **insn_per_second Parameter**

The `insn_per_second` parameter specifies the instruction execution rate for each vCPU, controlling the simulated CPU performance. This parameter is configured per CPU:

```lua
cpu = {
    moduletype = "cpu_arm_cortexA76";
    insn_per_second = 100000000;  -- 100 MIPS
    -- ... other CPU configuration
}
```

**Impact on CPU Timing:**

This parameter directly determines how CPU time is calculated from instruction counts. The MCIPS plugin converts executed instructions into simulation time using: `cpu_time = (instructions_executed / insn_per_second) * 1e9` nanoseconds.

***

## 4. Why Disable Quantum Keeper

When MCIPS plugin is enabled, the traditional **quantum keeper mechanism is automatically disabled** in the CPU implementation (`cpu.h`). This is essential because:

- **Quantum Keeper**: Uses wall-clock time and SystemC's quantum-based synchronization
- **MCIPS Plugin**: Uses instruction-count-based time calculation with custom synchronization windows

### **Time-Sync Strategy Selection**

`QemuCpu` does not branch on `mcips_enabled()` throughout its body. Instead it
owns a `CpuTimeSyncStrategy` (`std::unique_ptr<CpuTimeSyncStrategy> m_time_sync`),
chosen once in the constructor, and delegates every former branch point to it:

```cpp
// In cpu.h - the strategy is chosen once at construction
inline bool mcips_enabled() const { return m_inst.is_mcips_enabled(); }

if (mcips_enabled()) {
    m_time_sync = std::make_unique<McipsSync>(*this);          // time driven by the plugin
} else {
    m_time_sync = std::make_unique<QuantumKeeperSync>(*this);  // traditional quantum keeper
}
```

The quantum-keeper machinery itself (`create_quantum_keeper`, `kick_cb`,
`deadline_timer_cb`, `wait_for_work`, `sync_with_kernel`, `prepare_run_cpu`,
`end_of_loop_cb`, the coroutine/watch threads, ...) stays on `QemuCpu`,
unchanged. At each lifecycle/callback point `QemuCpu` calls a strategy hook
(`on_construct`, `on_after_cpu_created`, `on_before_end_of_elaboration`,
`on_halt_pre`/`on_halt_post`, `on_reset_finish`, `on_qk_start`,
`on_arm_deadline`, `get_local_time`, `set_local_time`, ...):

- `QuantumKeeperSync` forwards each hook to the matching `QemuCpu` method — the
  traditional behaviour.
- `McipsSync` overrides `on_end_of_elaboration()` to register `insn_per_second` with the plugin and `on_destroy()` to stop, unplug, and join the vCPU thread. Other lifecycle hooks use the base no-op.

Strategies hold no state of their own; they reach `QemuCpu`'s members through a
`m_qemu_cpu` back-reference. `QuantumKeeperSync` and `McipsSync` are nested
classes of `QemuCpu`, which grants them access to its private/protected members,
so the carefully-tuned member ordering and run loop are preserved. The abstract
`CpuTimeSyncStrategy` base stays a top-level class.

When MCIPS is enabled, the quantum keeper is not created, its deadline and synchronization callbacks are not registered, and its start/stop/sync operations become no-ops. `initiator_get_local_time()` and `initiator_set_local_time()` return `SC_ZERO_TIME` / do nothing through `McipsSync`. The separate `on_destroy()` hook stops, unplugs, and joins the vCPU thread before the QemuInstance is destroyed.

### **Halt and Reset with MCIPS**

With `McipsSync` active, the `on_halt_pre`/`on_halt_post` and `on_reset_finish`
hooks are no-ops, so `halt_cb` simply calls `lock_iothread` / `m_cpu.halt(val)`
/ `unlock_iothread`, and `reset_cb` performs the reset without any
quantum-keeper start/reset or kick event.

### **Tracked Async Work**

The CPU uses `make_tracked_async_job()` to wrap async jobs with `m_async_work_outstanding` tracking. The destructor waits (with a 500 ms timeout) for all in-flight jobs to complete before destroying the object, preventing use-after-free when async jobs hold captured references.

## 5. Monitor Support

The MCIPS plugin provides comprehensive **monitoring and debugging support** through the SystemC monitor interface:

### **Monitor Integration**
```cpp
// In monitor.cc - automatic discovery and registration
void monitor<BUSWIDTH>::end_of_elaboration()
{
    m_qks = find_sc_objects<gs::tlm_quantumkeeper_multithread>();
    m_mcips_plugins = find_sc_objects<McipsPlugin>();  // Auto-discover MCIPS plugins
}
```

### **Diagnostic JSON**

`get_mcips_status_json()` returns a JSON snapshot of the current state (reads without `m_mcips_mutex` so values may be slightly out of date) including: `qemu_time`, `n_cpus`, `active_vcpu_index`, and per-CPU details (`index`, `insn_per_second`, `delta_insn`, `cpu_time_ns`, `cpu_execution_status`).

***

## 6. SystemC Synchronization with sc_sync_window

QBox stores a **`sc_core::sc_sync_window<sc_sync_policy>`** object as `m_sync_sc` for coordination between QEMU and SystemC. The window's header and implementation come from an external SystemC dependency and are not included in this repository. This guide describes QBox's calls; it does not describe the window's internal policy or step implementation.

### **QBox sync-window usage**
- The plugin keeps one `m_sync_sc` window and registers `receive_window_cb()` with `register_sync_cb()`.
- QBox's `attach_sync_window()` checks `is_attached()` and calls `attach()` only when the window is detached.
- `set_systemc_window()` checks `is_attached()` before calling `async_set_window()`; it logs and skips the call when detached.

### **Attach and Detach**

**Attach** (`m_sync_sc.attach()`):
- QBox calls its attachment helper when an active CPU needs the window.
- The helper attaches only when `is_attached()` is false.
- During initialization, QBox checks `is_attached()` before the initial `async_set_window()` call. If detached, it logs a fatal message and asserts.

**Detach** (`m_sync_sc.detach(current_time)`):
- QBox calls its detach helper when all CPUs are idle; the helper calls `detach()` only when `is_attached()` is true.
- QBox passes the current QEMU time to the dependency's `detach()` call. The dependency defines the detach semantics.

### **Synchronization Flow**
```cpp
// When vCPU goes idle and no other vCPUs are active
if (!new_active) {
    m_idle_tick.notify(m_quantum);  // start idle pump
    detach_sync_window();           // QBox detaches the sync window
}

// When vCPU resumes from idle (first to wake)
if (m_active_vcpu.load() == nullptr) {
    m_active_vcpu.store(vcpu);
    if (!m_sync_sc.is_attached()) {
        m_sync_sc.attach();
    }
    set_systemc_window();  // push [qemu_time, qemu_time + quantum]
}
```

***

## 7. QEMU TCG Plugin API Integration and Event Registration

The MCIPS plugin leverages QEMU's **TCG Plugin API** through a sophisticated C++/C bridge system:

### **libidlinker Plugin and ID Communication**

When MCIPS is enabled, `QemuInstance` requests the platform-specific `libidlinker` filename: `libidlinker.dll` on Windows, `libidlinker.dylib` on macOS, and `libidlinker.so` on Linux. The QBox integration expects the plugin to provide a `qemu_plugin_id_t` and communicate it through `global_set_cci_param()`, which stores it in a `cci::cci_param<uint64_t>`. The external plugin's API implementation, fetched library, and build paths are dependency implementation details and are not verified by this guide.

### **TCG Plugin API Access**
QBox accesses QEMU plugin function pointers through `LibQemuExports` and the `LibQemu::plugin_api()` accessor. How the external libqemu dependency generates this table (including `exports.py`) and its build paths are dependency implementation details and are not verified by this guide.
- **Type Safety**: Plugin types (`qemu_plugin_id_t`, `qemu_plugin_tb`, callback typedefs) come directly from `qemu-plugin.h` via a glib-safe wrapper, so consumers don't need to redeclare anything.

### **LibQemuPlugin Base Class**

The **LibQemuPlugin** base class (`libqemu-plugin.h`) inherits from `sc_core::sc_module` and holds a reference to `qemu::LibQemu`. It provides:

- **PluginHandle**: Heap-allocated, intentionally leaked at plugin destruction. Every callback passes `handle_as_userdata()` so the dispatch can resolve to the C++ instance with no map lookup. Outliving the plugin lets stale callbacks observe `alive == false` and exit early instead of dereferencing freed memory.
- **Single userdata dispatch**: There is exactly one dispatch path. Subclasses register a capture-less lambda (which decays to the C function pointer the plugin API expects) directly via `m_inst.plugin_api().qemu_plugin_register_*_cb(...)`; the lambda calls `LibQemuPlugin::dispatch_userdata(userdata, body)` to recover the instance. The base class deliberately mirrors no callback signature — only the subclass registration sites do — keeping it decoupled from upstream plugin-API churn.
- **`shutdown_bridge()`**: Stops bridge dispatch and waits for in-flight callbacks to drain. Idempotent — `m_handle->alive`'s exchange is the once-guard. Flips the handle dead, then spins until `m_handle->refcount` reaches zero. The handle itself is intentionally leaked.

### **Event Registration Process**
The plugin registers the vCPU-translation callback during `end_of_elaboration()`. QEMU calls `vcpu_tb_trans()` for each translated block; that function registers the inline instruction counter and quota callback for that block. Other plugin callbacks are also registered during elaboration:

```cpp
// Every callback is registered as a capture-less lambda forwarding through
// dispatch_userdata(), with handle_as_userdata() as the userdata.
m_inst.plugin_api().qemu_plugin_register_vcpu_tb_trans_cb(
    m_id,
    [](qemu_plugin_tb* tb, void* userdata) {
        LibQemuPlugin::dispatch_userdata(
            userdata, [&](LibQemuPlugin* p) { static_cast<McipsPlugin*>(p)->vcpu_tb_trans(tb); });
    },
    handle_as_userdata());
// ... vcpu_init, vcpu_resume, vcpu_idle registered the same way ...

// Time callback (returns int64_t; SystemC time is the dead-handle fallback)
m_inst.plugin_api().qemu_plugin_register_time_cb(
    m_time_handle,
    [](void* userdata) -> int64_t {
        int64_t result = static_cast<int64_t>(sc_core::sc_time_stamp().to_seconds() * NSEC_IN_ONE_SEC);
        LibQemuPlugin::dispatch_userdata(
            userdata, [&](LibQemuPlugin* p) { result = static_cast<McipsPlugin*>(p)->get_qemu_clock(nullptr); });
        return result;
    },
    handle_as_userdata());
```

## How Everything Works Together

The MCIPS plugin creates a sophisticated multi-layered synchronization system:

1. **QEMU Layer**: The plugin ID flow depends on the external libidlinker implementation; TCG instrumentation counts instructions inline
2. **Dispatch Layer**: Capture-less lambdas pass QEMU callbacks to SystemC components through a single `dispatch_userdata()` path; refcounting keeps objects alive during callbacks
3. **Plugin Layer**: McipsPlugin turns instruction counts into simulation time, manages CPU states (IDLE/RUNNING/PAUSED), and runs the idle time pump
4. **SystemC Layer**: QBox uses the synchronization-window interface to coordinate time advancement between QEMU and SystemC
5. **Monitor Layer**: Provides visibility into the entire system state via JSON diagnostics

***
