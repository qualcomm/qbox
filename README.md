# QBox

SystemC/QEMU co-simulation framework for virtual platform
development.

## Overview

QBox integrates QEMU into SystemC as a TLM-2.0 model, enabling
cycle-approximate simulation of complete hardware platforms. The
project consists of:

- **libqemu-cxx** -- C++ wrapper around QEMU
- **libqbox** -- SystemC TLM-2.0 integration layer for QEMU
- **Base components** -- SystemC models (routers, memories,
  loaders, exclusive monitor)
- **QEMU device models** -- CPUs, interrupt controllers, UARTs,
  timers, PCI devices, and more
- **Example platforms** -- Reference implementations in `platforms/`

### Supported Architectures

- **ARM:** Cortex-A53/A55/A76/A710, Cortex-M7/M55,
  Cortex-R5/R52, Neoverse-N1/N2
- **RISC-V:** riscv32, riscv64, SiFive X280
- **Hexagon:** Hexagon DSP

## Requirements

Install dependencies using the provided script (supports Ubuntu
20.04/22.04/24.04 and macOS 14/15):

```bash
sudo scripts/install_dependencies.sh
```

## Building

QBox uses [CMake Presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)
(requires CMake 3.21+) to provide ready-made build configurations:

```bash
cmake --preset gcc
cmake --build --preset gcc --parallel
ctest --preset gcc
```

### Available Presets

| Preset | Compiler | Build Type | Notes |
|--------|----------|------------|-------|
| `gcc` | GCC | Release | Default preset |
| `gcc-debug` | GCC | Debug | |
| `clang` | Clang | Release | Uses `cmake/clang-toolchain.cmake` |
| `clang-debug` | Clang | Debug | Enables `MALLOC_CHECK_=3` |
| `clang-lto` | Clang | Release | Link-time optimization with lld |
| `mac` | Apple Clang | Release | macOS builds |
| `mac-debug` | Apple Clang | Debug | macOS debug builds |

List all presets with `cmake --list-presets`.

Presets can be overridden from the command line:

```bash
cmake --preset gcc -DLIBQEMU_TARGETS="aarch64;riscv64"
```

Create a `CMakeUserPresets.json` file for personal overrides
(git-ignored).

### Manual Configuration

If your CMake version is older than 3.21 or you need full control:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCPM_DOWNLOAD_ALL=ON
cmake --build build --parallel
cmake --install build
```

### CMake Options

| Option | Description |
|--------|-------------|
| `CMAKE_INSTALL_PREFIX` | Install directory for the package and binaries |
| `CMAKE_BUILD_TYPE` | `Debug` or `Release` |
| `LIBQEMU_TARGETS` | Semicolon-separated list of QEMU targets (e.g., `aarch64;riscv64;hexagon`) |
| `CPM_DOWNLOAD_ALL` | Download all dependencies via CPM |
| `CPM_SOURCE_CACHE` | Directory to cache downloaded packages |
| `ENABLE_PYTHON_BINDER` | Enable the PythonBinder component (default: ON). Note: `WITHOUT_PYTHON_BINDER` is deprecated. |
| `GS_ENABLE_LTO` | Enable Link-Time Optimization |
| `UBUNTU_ARCH` | Architecture for Ubuntu platform builds (`aarch64` or `riscv64`) |

### Dependency Management

QBox uses [CPM](https://github.com/cpm-cmake/CPM.cmake) to find
and/or download missing dependencies. CPM uses CMake's standard
`find_package` mechanism to locate installed packages.

**Environment variables:**

- `SYSTEMC_HOME` -- Path to a locally installed SystemC
- `CCI_HOME` -- Path to a locally installed SystemC CCI

**Specifying package locations:**

- `<package>_ROOT` -- Point to a specific installed package
  location
- `CPM_<package>_SOURCE_DIR` -- Use your own source directory
  for a package
- `CMAKE_MODULE_PATH` -- Additional search paths for CMake
  modules

Example using a custom SystemC CCI source:

```bash
cmake -B build -DCPM_SystemCCCI_SOURCE=/path/to/your/cci/source
```

**Offline / cached builds:**

```bash
cmake -B build -DCPM_SOURCE_CACHE=$(pwd)/Packages
```

This populates the `Packages` directory. If a directory named
`Packages` exists at the project root, the build system will
automatically use it as a source cache.

### Testing

```bash
# Run all tests (using preset -- output-on-failure is enabled by default)
ctest --preset gcc

# Run a specific test
ctest --preset gcc -R <test_name>

# Without presets
ctest --test-dir build --output-on-failure
```

### Troubleshooting

**CMake cache issues:** CMake caches compiled modules in
`~/.cmake/`. If you are picking up the wrong version of a
module, it is safe to delete this cache directory.

**Corrupted build directory:** Remove and reconfigure:

```bash
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

**Missing QEMU targets:** Rebuild with the target included:

```bash
cmake -B build -DLIBQEMU_TARGETS="aarch64;riscv64"
```

## MCIPS Plugin (Multi-Core Instructions Per Second Plugin)

The **MCIPS plugin** is a SystemC/C++ component that provides time synchronization between **QEMU** and **SystemC** by leveraging QEMU's TCG (Tiny Code Generator) plugin API. It enables deterministic multi-core simulation with instruction-level timing accuracy.

> **Note:** The MCIPS plugin currently supports **TCG mode only**.

***

### 1. Selecting the MCIPS Time-Sync Strategy from Lua Configuration

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

### 2. How the MCIPS Plugin Works

The MCIPS plugin is implemented as a **SystemC module** (`McipsPlugin` in `mcips-plugin.h`) that inherits from the **`LibQemuPlugin`** base class. Here is how it operates:

#### **Thread Model**

| Thread | Entry points |
|---|---|
| QEMU vCPU (BQL held) | `vcpu_init`, `vcpu_idle`, `vcpu_resume`, `vcpu_tb_trans`, `vcpu_tb_exec_cond`, `cpu_end_delta_quota` |
| SystemC | `receive_window_cb`, `idle_tick_method` |
| QEMU timer | `get_qemu_clock` (no locks) |

**Lock ordering:** `BQL → m_mcips_mutex` (never reversed).

#### **Lock-Free Atomics**

| Variable | Ordering | Purpose |
|---|---|---|
| `m_shutdown` | seq_cst | Shutdown flag; checked on all entry paths |
| `m_inflight_cb` | seq_cst | Shutdown drain counter for `receive_window_cb` |
| `m_qemu_time_ns` | release/acquire | Nanosecond shadow of `m_qemu_time` for `get_qemu_clock` |
| `m_active_vcpu` | acquire (lock-free) / relaxed (under mutex) | Active CPU pointer |
| `m_first_vcpu_initialized` | release/acquire | Gates the startup idle pump until the first vCPU exists |

#### **QEMU TCG Plugin Integration**
- **libidlinker Plugin**: Runs QEMU's libidlinker shared library (`libidlinker.so`/`.dylib`)
- **Plugin ID Communication**: libidlinker calls `global_set_cci_param()` to send plugin ID back to QBox; the ID is stored as a `cci::cci_param<uint64_t>` (`m_id`)
- **Event Registration**: Every callback is registered directly via `m_inst.plugin_api().qemu_plugin_register_*_cb(...)` as a capture-less lambda that calls `dispatch_userdata()`, passing `handle_as_userdata()` as the userdata.
- **Callback Dispatch**: Each capture-less lambda decays to the C function pointer QEMU expects and routes the callback to the C++ member function via `dispatch_userdata()`

#### **Instruction-Based Timing Model**
- **Instruction Quota**: Each vCPU runs a global quantum (instruction count limit) before synchronization
- **Delta Tracking**: Tracks `delta_insn` (instructions executed since last sync) per vCPU using QEMU scoreboards
- **Time Calculation**: Converts instruction count to simulation time using `insn_per_second` parameter. `qemu_time_now()` has an overload accepting the active pointer to avoid redundant atomic loads when called under `m_mcips_mutex`.
- **Active vCPU Management**: Maintains an atomic pointer (`m_active_vcpu`) to the currently active (executing) vCPU

#### **Translation Block Instrumentation**
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

#### **Multi-vCPU State Management**
- **vCPU States (`vCPUTimeStatus`)**: Each vCPU can be in `IDLE`, `RUNNING`, or `PAUSED` state
- **Per-vCPU data (`vCPUTime` struct)**: Tracks `index`, `insn_per_second`, `delta_insn`, `cpu_time`, `cpu_execution_status`
- **Active vCPU Selection**: When the active CPU goes idle, `select_active_vcpu()` picks the next available non-idle, non-halted CPU
- **Slowest CPU Tracking**: `slowest_active_cpu()` finds the CPU that is furthest behind in simulation time
- **Pause/Resume Logic**: `pause_if_ahead()` pauses vCPUs that advance too far ahead; `resume_if_behind()` resumes those that fall behind. The active CPU pointer is loaded once and threaded through both functions to minimize atomic contention.

#### **Idle Time Pump**
When all CPUs are idle (e.g., all in WFI/WFE state), the plugin starts an **idle time pump** (`m_idle_tick` / `idle_tick_method()`) so that QEMU timers can still fire and move simulation time forward. `detach` alone does not advance time — the SystemC kernel only steps toward a pending timed event, and with every CPU idle nothing else produces one. The pump is the `m_idle_tick.notify(m_quantum)` itself: the scheduled event forces the kernel to step time to deliver it. It is kicked from `vcpu_idle()` when the last active CPU goes idle, and from `get_qemu_clock()` at startup (gated by `m_first_vcpu_initialized`, before any vCPU exists). `vcpu_resume()` re-attaches the window when the first CPU wakes, so QEMU retakes the clock and the pump lapses. When no CPU is active, `idle_tick_method()` does nothing after loading `m_active_vcpu` — it is a pure "sink" for the async_event, and just being scheduled is enough to advance `sc_time_stamp()`.

#### **Atomic Time Copy**
`m_qemu_time_ns` is an atomic copy of `m_qemu_time` in nanoseconds. `get_qemu_clock()` runs on QEMU's timer thread without holding `m_mcips_mutex`, so it reads the base time from this shadow (release/acquire) rather than the non-atomic `sc_time m_qemu_time` — reading the latter off-mutex while vCPU threads write it under the mutex would be a torn read. It is updated by `sync_qemu_time_ns()` every time `m_qemu_time` changes while holding `m_mcips_mutex`.

#### **Shutdown and Cleanup**

`McipsPlugin` implements deterministic shutdown through multiple guards:

- **`m_shutdown` (atomic bool)**: Set during `end_of_simulation()` and checked at the start of every callback so late calls exit early.
- **`m_inflight_cb` (atomic int)**: Counts how many `receive_window_cb` calls are running right now; shutdown waits for this to reach zero before continuing.
- **`shutdown_bridge()`**: Called from both `end_of_simulation()` and the destructor. Safe to call more than once (the second call does nothing).
- **Detach on shutdown**: If the sync window is still attached, it is detached with the current `m_qemu_time` so SystemC can keep running on its own.

##### Shutdown Double-Check Pattern

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

### 3. Timing Parameters and Synchronization Accuracy

The MCIPS plugin's synchronization behavior is controlled by two critical timing parameters:

#### **quantum_ns Parameter**

The `quantum_ns` parameter defines the global quantum (in nanoseconds) used for time synchronization between SystemC and QEMU. This parameter is set at the platform level:

```lua
platform = {
    quantum_ns = 100000;  -- Recommended: 100 microseconds
    -- ... other platform configuration
}
```

**Impact on Synchronization:**

The `quantum_ns` value controls the trade-off between synchronization accuracy and performance. Smaller values increase synchronization frequency and timing precision at the cost of overhead. Larger values reduce overhead but decrease timing accuracy.

**Recommendation**: Use `quantum_ns = 100000` (100 microseconds) as a starting point.

#### **insn_per_second Parameter**

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

### 4. Why Disable Quantum Keeper

When MCIPS plugin is enabled, the traditional **quantum keeper mechanism is automatically disabled** in the CPU implementation (`cpu.h`). This is essential because:

- **Quantum Keeper**: Uses wall-clock time and SystemC's quantum-based synchronization
- **MCIPS Plugin**: Uses instruction-count-based time calculation with custom synchronization windows

#### **Time-Sync Strategy Selection**

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
- `McipsSync` overrides only `on_end_of_elaboration()` (to register
  `insn_per_second` with the plugin); every other hook keeps the empty base
  no-op. The set of hooks it does *not* override documents exactly how much of
  the CPU lifecycle MCIPS participates in.

Strategies hold no state of their own; they reach `QemuCpu`'s members through a
`m_qemu_cpu` back-reference. `QuantumKeeperSync` and `McipsSync` are nested
classes of `QemuCpu`, which grants them access to its private/protected members,
so the carefully-tuned member ordering and run loop are preserved. The abstract
`CpuTimeSyncStrategy` base stays a top-level class.

When MCIPS is enabled the machinery is therefore simply never reached: no
quantum keeper is created, no `end_of_loop_cb`/`kick_cb`/`deadline_timer_cb`
callbacks are registered, no coroutine or external-event watch thread is
spawned, and the quantum-keeper start/stop/sync calls throughout the CPU
lifecycle (constructor, `before_end_of_elaboration`, `start_of_simulation`,
`halt_cb`, `reset_cb`, destructor, ...) become no-ops. `initiator_get_local_time()`
and `initiator_set_local_time()` return `SC_ZERO_TIME` / do nothing via the
`McipsSync` hooks.

#### **Halt and Reset with MCIPS**

With `McipsSync` active, the `on_halt_pre`/`on_halt_post` and `on_reset_finish`
hooks are no-ops, so `halt_cb` simply calls `lock_iothread` / `m_cpu.halt(val)`
/ `unlock_iothread`, and `reset_cb` performs the reset without any
quantum-keeper start/reset or kick event.

#### **Tracked Async Work**

The CPU uses `make_tracked_async_job()` to wrap async jobs with `m_async_work_outstanding` tracking. The destructor waits (with a 500 ms timeout) for all in-flight jobs to complete before destroying the object, preventing use-after-free when async jobs hold captured references.

### 5. Monitor Support

The MCIPS plugin provides comprehensive **monitoring and debugging support** through the SystemC monitor interface:

#### **Monitor Integration**
```cpp
// In monitor.cc - automatic discovery and registration
void monitor<BUSWIDTH>::end_of_elaboration()
{
    m_qks = find_sc_objects<gs::tlm_quantumkeeper_multithread>();
    m_mcips_plugins = find_sc_objects<McipsPlugin>();  // Auto-discover MCIPS plugins
}
```

#### **Diagnostic JSON**

`get_mcips_status_json()` returns a JSON snapshot of the current state (reads without `m_mcips_mutex` so values may be slightly out of date) including: `qemu_time`, `n_cpus`, `active_vcpu_index`, and per-CPU details (`index`, `insn_per_second`, `delta_insn`, `cpu_time_ns`, `cpu_execution_status`).

***

### 6. SystemC Synchronization with sc_sync_window

The MCIPS plugin uses **`sc_core::sc_sync_window<sc_sync_policy>`** (defined in `sync_window.h`) for coordinated time advancement between QEMU and SystemC. This is a template class parameterised by a sync policy.

#### **sc_sync_window Architecture**
- **Template Policy**: `sc_sync_window` takes a sync policy as a template (e.g., `sc_sync_policy_tlm_quantum` uses the TLM global quantum, `sc_sync_policy_in_sync` tracks pending work). The policy tells the window how big each step is and whether to stay attached when idle.
- **Single Window**: The MCIPS plugin has one `sc_sync_window<sc_sync_policy_tlm_quantum>` (`m_sync_sc`) that talks to SystemC.
- **Time Windows**: Each window has a `{from, to}` range stored in `sc_current_window`.
- **Sweep/Step Model**: Uses `SC_METHOD` helpers inside -- `sweep_helper()` moves time to the start of the next window, `step_helper()` pauses SystemC at the end until a new window comes in.
- **Observer Event**: Uses `sc_ob_event` (or `gs::observer_event` as fallback) to notify at window edges.
- **Callback Registration**: Uses `register_sync_cb()` to get window updates through `receive_window_cb()`.

#### **Attach and Detach**

**Attach** (`m_sync_sc.attach()`):
- Turns on time synchronization between QEMU and SystemC.
- Used when at least one CPU is active and running.
- After attaching, you must call `async_set_window()` to send the first window.

**Detach** (`m_sync_sc.detach(current_time)`):
- Opens the window to `[current_time, max_time]` and stops synchronization.
- Used when all CPUs are idle (e.g., all in WFI/WFE state).
- SystemC can keep running without waiting for QEMU.

> **Important**: `async_set_window()` will crash if the window is not attached. Always attach first.

#### **Synchronization Flow**
```cpp
// When vCPU goes idle and no other vCPUs are active
if (!new_active) {
    m_idle_tick.notify(m_quantum);  // start idle pump
    detach_sync_window();           // opens window to [qemu_time, max_time]
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

### 7. QEMU TCG Plugin API Integration and Event Registration

The MCIPS plugin leverages QEMU's **TCG Plugin API** through a sophisticated C++/C bridge system:

#### **libidlinker Plugin and ID Communication**
QBox runs QEMU's **libidlinker** plugin to obtain a unique plugin ID:
- **Plugin Loading**: QBox loads `libidlinker.so` (Linux) or `libidlinker.dylib` (macOS) into QEMU
- **ID Generation**: libidlinker generates a unique `qemu_plugin_id_t` for the plugin instance
- **ID Communication**: libidlinker calls the C function `global_set_cci_param(key, plugin_id)` to communicate the ID back to QBox
- **CCI Integration**: The ID is stored as a `cci::cci_param<uint64_t>` and used by the McipsPlugin instance

#### **TCG Plugin API Access**
QBox calls QEMU's plugin API through the auto-generated `LibQemuExports` table, reached via the `LibQemu::plugin_api()` accessor:
- **Auto-generated dispatch table**: Plugin function pointers are populated by libqemu's `exports.py` machinery alongside the rest of the libqemu API.
- **Type Safety**: Plugin types (`qemu_plugin_id_t`, `qemu_plugin_tb`, callback typedefs) come directly from `qemu-plugin.h` via a glib-safe wrapper, so consumers don't need to redeclare anything.

#### **LibQemuPlugin Base Class**

The **LibQemuPlugin** base class (`libqemu-plugin.h`) inherits from `sc_core::sc_module` and holds a reference to `qemu::LibQemu`. It provides:

- **PluginHandle**: Heap-allocated, intentionally leaked at plugin destruction. Every callback passes `handle_as_userdata()` so the dispatch can resolve to the C++ instance with no map lookup. Outliving the plugin lets stale callbacks observe `alive == false` and exit early instead of dereferencing freed memory.
- **Single userdata dispatch**: There is exactly one dispatch path. Subclasses register a capture-less lambda (which decays to the C function pointer the plugin API expects) directly via `m_inst.plugin_api().qemu_plugin_register_*_cb(...)`; the lambda calls `LibQemuPlugin::dispatch_userdata(userdata, body)` to recover the instance. The base class deliberately mirrors no callback signature — only the subclass registration sites do — keeping it decoupled from upstream plugin-API churn.
- **`shutdown_bridge()`**: Stops bridge dispatch and waits for in-flight callbacks to drain. Idempotent — `m_handle->alive`'s exchange is the once-guard. Flips the handle dead, then spins until `m_handle->refcount` reaches zero. The handle itself is intentionally leaked.

#### **Event Registration Process**
The MCIPS plugin registers multiple event types during `end_of_elaboration()`:

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

### How Everything Works Together

The MCIPS plugin creates a sophisticated multi-layered synchronization system:

1. **QEMU Layer**: libidlinker plugin provides unique ID; TCG instrumentation counts instructions inline
2. **Dispatch Layer**: Capture-less lambdas pass QEMU callbacks to SystemC components through a single `dispatch_userdata()` path; refcounting keeps objects alive during callbacks
3. **Plugin Layer**: McipsPlugin turns instruction counts into simulation time, manages CPU states (IDLE/RUNNING/PAUSED), and runs the idle time pump
4. **SystemC Layer**: `sc_sync_window` talks to the SystemC kernel through sweep/step methods to move time forward
5. **Monitor Layer**: Provides visibility into the entire system state via JSON diagnostics

***

## Quick Start: Ubuntu Platform (AArch64)

```bash
# Build the rootfs image
cd platforms/ubuntu/fw/
./build_linux_dist_image.sh -s 4G -p xorg,pciutils -a aarch64
cd ../../..

# Build QBox with AArch64 target
cmake -B build -DLIBQEMU_TARGETS=aarch64
cmake --build build --parallel

# Run the virtual platform
./build/platforms/platforms-vp -l platforms/ubuntu/conf_aarch64.lua
```

## Documentation

- **[Configuration](docs/configuration.md)** -- CCI parameters,
  Lua configuration, YAML, ConfigurableBroker

### Component Libraries

- **[libqbox](docs/libqbox.md)** -- QEMU/SystemC integration,
  CPUs, interrupt controllers, UARTs, VNC, parallelism
- **[libqemu-cxx](docs/libqemu-cxx.md)** -- Low-level QEMU C++
  wrapper
- **[libgssync](docs/libgssync.md)** -- Synchronization policies
  and suspend/unsuspend interface
- **[libgsutils](docs/libgsutils.md)** -- CCI utilities and TLM
  port types
- **[Base components](docs/base-components.md)** -- Router,
  memory, loader, memory dumper
- **[Extra components](docs/extra-components.md)** -- GPEX, NVME,
  OpenCores Ethernet
- **[PythonBinder](docs/python-binder.md)** -- Python integration
  for SystemC models
- **[Networking](docs/networking.md)** -- SystemC Ethernet MAC
  setup and host configuration
- **[Character backends](docs/backends.md)** -- stdio, socket,
  and file backends for UART I/O
- **[Monitor](docs/monitor.md)** -- Web-based simulation
  monitoring interface

### Platforms

- **[Ubuntu](docs/platforms/ubuntu.md)** -- Ubuntu Linux platform
  (AArch64 and RISC-V 64)

### Tutorials and Examples

- **[hello-qbox](examples/hello-qbox/)** -- Step-by-step
  tutorial that walks you through building a minimal AArch64
  virtual platform (Cortex-A53, RAM, UART) from scratch

## C++ Standard

QBox requires C++14 and is compatible with SystemC versions from
SystemC 2.3.1a.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

See [LICENSE](LICENSE).
