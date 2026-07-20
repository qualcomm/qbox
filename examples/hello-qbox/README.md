# How to Build a Virtual Platform with Qbox

Qbox is a SystemC-based framework for building virtual platforms —
software models of hardware systems. It integrates QEMU as a CPU
simulation engine and exposes it as a standard TLM-2.0 component, so
you can compose complex SoC models by wiring together CPUs, memory,
UARTs, interrupt controllers, and other peripherals using Lua
configuration files.

This tutorial walks you through building a working virtual platform
from scratch, using a minimal bare-metal AArch64 program as the
firmware. The result is the `hello-qbox` example: a Cortex-A53 with
RAM and a UART that prints `Hello from Qbox!` to your terminal.

For prerequisites and Qbox build instructions, see the
[Qbox README](../../README.md).

---

## Table of Contents

1. [Core Concepts](#core-concepts)
2. [Platform Architecture](#platform-architecture)
3. [Building a Platform Step by Step](#building-a-platform-step-by-step)
4. [Building the Example](#building-the-example)
5. [Running Your Platform](#running-your-platform)
6. [Debugging with GDB](#debugging-with-gdb)
7. [Adding More Peripherals](#adding-more-peripherals)
8. [Next Steps](#next-steps)

---

## Core Concepts

Before writing any configuration, it helps to understand the four key
ideas behind Qbox.

### SystemC and TLM-2.0
SystemC is an IEEE-standard hardware simulation framework written in
C++. Qbox components communicate with each other using TLM-2.0
(Transaction Level Modeling), where one component initiates a
transaction (a read or write) and another responds to it. Connections
between components are called *sockets* — an initiator socket on one
component binds to a target socket on another.


### The Router
Most platforms have a central address router. Every peripheral
registers an address range with the router, and when a CPU issues a
memory access, the router forwards it to the right component. Think
of it as the system bus.

### The QEMU Instance
The CPU is not a SystemC component itself — it lives inside QEMU. The
`QemuInstance` component wraps a QEMU process and makes it accessible
to the rest of the SystemC simulation. CPU components (like
`cpu_arm_cortexA53`) are then attached to that instance.

### Lua Configuration
Platforms are defined in Lua files, not C++ code. Each component is
described by a Lua table with a `moduletype` key and whatever
parameters the component needs. Bindings between components use `"&"`
references. These references are relative to the enclosing
container — write `"&router.initiator_socket"`, not
`"&platform.router.initiator_socket"`.

---

## Platform Architecture

A minimal ARM platform needs these components:

```
┌─────────────────────────────────────────────────────┐
│ platform (Container)                                 │
│                                                      │
│  ┌──────────┐  TLM-2.0   ┌───────────────────────┐  │
│  │  cpu_0   │◄──────────►│       router           │  │
│  │ (A53)    │            │  [address dispatcher]  │  │
│  └──────────┘            └──┬──────────┬──────────┘  │
│       │                     │          │              │
│  ┌────┴──────┐         ┌────┴────┐ ┌───┴──────────┐  │
│  │ qemu_inst │         │  ram_0  │ │ pl011_uart_0 │  │
│  │ (QEMU)    │         │ (256MB) │ │  (console)   │  │
│  └───────────┘         └─────────┘ └──────────────┘  │
└─────────────────────────────────────────────────────┘
```

The `cpu_0` component needs a `qemu_inst` (to run code) and a
connection to the router (so it can access memory and peripherals).
For interrupt-driven platforms, add a `gic_0` interrupt controller
(see [Adding More Peripherals](#adding-more-peripherals)). The
`pl011_uart_0` needs a backend — either stdio or a TCP socket — to
expose the serial console.

---

## Building a Platform Step by Step

### 1. Create the configuration file

Create a file called `platform.lua`:

```lua
-- Helper to resolve paths relative to this config file
function script_path()
    local src = debug.getinfo(2, "S").source:sub(2)
    return src:match("(.*/)")
end
local base = script_path()

platform = {
    moduletype = "Container",

    -- How many nanoseconds of simulation time to advance per quantum.
    -- Larger values improve speed but reduce synchronization accuracy.
    quantum_ns = 10000000,
}
```

### 2. Add the address router

The router dispatches memory transactions to the right peripheral
based on address.

```lua
platform.router = {
    moduletype = "router",
    log_level = 0,
}
```

### 3. Add RAM

This creates a 256 MB block of RAM starting at address `0x80000000`
— the address the example binary is linked to run at.

```lua
platform.ram_0 = {
    moduletype = "gs_memory",
    target_socket = {
        address = 0x80000000,
        size    = 0x10000000,   -- 256 MB
        bind    = "&router.initiator_socket",
    },
}
```

### 4. Add the QEMU instance

The QEMU instance wraps the QEMU engine and provides CPU models with
a place to run.

```lua
platform.qemu_inst_mgr = {
    moduletype = "QemuInstanceManager",
}

platform.qemu_inst = {
    moduletype   = "QemuInstance",
    args         = { "&qemu_inst_mgr", "AARCH64" },
    accel        = "tcg",   -- use "kvm" on native aarch64 Linux
    sync_policy  = "multithread-unconstrained",
}
```

### 5. Add an ARM Cortex-A53 CPU

```lua
platform.cpu_0 = {
    moduletype   = "cpu_arm_cortexA53",
    args         = { "&qemu_inst" },
    mem          = { bind = "&router.target_socket" },
    rvbar        = 0x80000000,
    has_el3      = true,
    has_el2      = true,
    psci_conduit = "hvc",
}
```

- `mem` connects the CPU's memory-access initiator to the
  router. Without it, the CPU has no path to RAM or peripherals
  and the simulation fails at startup with an unbound-port
  error.
- `rvbar` sets the Reset Vector Base Address Register — the
  address the CPU jumps to when it comes out of reset. This
  must match the load address of your firmware (`0x80000000`
  in this example). Without it, the CPU starts executing at
  `0x0`, which has no memory mapped in this platform.

### 6. Add a UART

The PL011 is the standard ARM UART. Connect it to stdio so you can
interact with the console directly.

For polled TX (writing to the data register without interrupts), no
IRQ connection is needed.

```lua
platform.charbackend_stdio_0 = {
    moduletype = "char_backend_stdio",
    read_write = true,
}

platform.pl011_uart_0 = {
    moduletype    = "Pl011",
    dylib_path    = "uart-pl011",
    target_socket = {
        address = 0x09000000,
        size    = 0x1000,
        bind    = "&router.initiator_socket",
    },
    backend_socket = {
        bind = "&charbackend_stdio_0.biflow_socket" },
}
```

#### How `moduletype` and `dylib_path` work.
Qbox components are shared libraries loaded at runtime. The
`moduletype` string is appended with `.so` and passed to
`dlopen`. When the `.so` filename differs from the class name
it registers, set `dylib_path` to the actual filename (without
the `.so` suffix). For example, `char_backend_stdio` maps directly
to `char_backend_stdio.so`. The PL011 class is called `Pl011` but
lives in `uart-pl011.so`, so it needs `dylib_path =
"uart-pl011"` to tell the loader which library to open.

### 7. Load the binary

The loader reads ELF files and writes their contents into
memory at startup.

```lua
platform.load = {
    moduletype = "loader",
    initiator_socket = { bind = "&router.target_socket" },
    { elf_file = base .. "build/hello.elf" },
}
```

`initiator_socket` connects the loader to the router so it can
issue write transactions to place the ELF contents into RAM.
Without this binding, the loader has no path to memory and the
simulation fails with an unbound-port error.

### Complete configuration

Your `platform.lua` should now look like this (steps 1–5,
7–8; the GIC from step 6 is optional for polled-only
platforms like this one):

```lua
function script_path()
    local src = debug.getinfo(2, "S").source:sub(2)
    return src:match("(.*/)")
end
local base = script_path()

platform = {
    moduletype  = "Container",
    quantum_ns  = 10000000,

    router = { moduletype = "router", log_level = 0 },

    ram_0 = {
        moduletype    = "gs_memory",
        target_socket = {
            address = 0x80000000,
            size    = 0x10000000,  -- 256 MB
            bind    = "&router.initiator_socket",
        },
    },

    qemu_inst_mgr = { moduletype = "QemuInstanceManager" },

    qemu_inst = {
        moduletype  = "QemuInstance",
        args        = { "&qemu_inst_mgr", "AARCH64" },
        accel       = "tcg",
        sync_policy = "multithread-unconstrained",
    },

    cpu_0 = {
        moduletype   = "cpu_arm_cortexA53",
        args         = { "&qemu_inst" },
        mem          = { bind = "&router.target_socket" },
        rvbar        = 0x80000000,
        has_el3      = true,
        has_el2      = true,
        psci_conduit = "hvc",
    },

    charbackend_stdio_0 = {
        moduletype = "char_backend_stdio",
        read_write = true,
    },

    pl011_uart_0 = {
        moduletype    = "Pl011",
        dylib_path    = "uart-pl011",
        target_socket = {
            address = 0x09000000,
            size    = 0x1000,
            bind    = "&router.initiator_socket",
        },
        backend_socket = {
            bind = "&charbackend_stdio_0.biflow_socket" },
    },

    load = {
        moduletype = "loader",
        initiator_socket = {
            bind = "&router.target_socket" },
        { elf_file = base .. "build/hello.elf" },
    },
}
```

### The C++ host code (main.cc)

While `platform.lua` describes *what* components exist and how they
connect, `main.cc` is the C++ program that *runs* the simulation. It
is intentionally minimal — most of the heavy lifting is done by the
Qbox libraries.

```cpp
#include <systemc>
#include <cci_configuration>
#include <cciutils.h>
#include <argparser.h>
#include <module_factory_container.h>

class GreenSocsPlatform : public gs::ModuleFactory::Container
{
protected:
    cci::cci_param<int> m_quantum_ns;
    cci::cci_param<int> m_gdb_port;

public:
    GreenSocsPlatform(const sc_core::sc_module_name& n)
        : gs::ModuleFactory::Container(n)
        , m_quantum_ns("quantum_ns", 1000000, "TLM-2.0 global quantum in ns")
        , m_gdb_port("gdb_port", 0, "GDB port")
    {
        using tlm_utils::tlm_quantumkeeper;

        sc_core::sc_time global_quantum(m_quantum_ns, sc_core::SC_NS);
        tlm_quantumkeeper::set_global_quantum(global_quantum);
    };
};
```

#### The `GreenSocsPlatform` class

This class inherits from `gs::ModuleFactory::Container`, the base class
that handles dynamic component instantiation. When the simulation starts,
the `Container` reads `platform.lua` and creates the components defined
there — CPUs, memory, UARTs, and so on — automatically.

The two CCI parameters (`m_quantum_ns` and `m_gdb_port`) can be set
from the Lua file or overridden on the command line with `--param`.
The constructor sets the TLM-2.0 global quantum, which controls how
often QEMU synchronizes with the rest of the SystemC simulation.

#### The `sc_main` function

```cpp
int sc_main(int argc, char* argv[])
{
    scp::LoggingGuard logging_guard(...);

    gs::ConfigurableBroker m_broker{};
    cci::cci_originator orig("sc_main");
    cci::cci_param<int> p_log_level{ "log_level", 0, ... };
    auto broker_h = m_broker.create_broker_handle(orig);
    ArgParser ap{ broker_h, argc, argv };

    GreenSocsPlatform platform("platform");

    sc_core::sc_start();

    // Print simulation statistics
    return 0;
}
```

`sc_main` is the entry point for any SystemC application. The key steps
are:

1. **Initialize logging** — Sets up the SCP logging framework that
   components use to print debug and error messages.

2. **Create the CCI broker** — CCI (Configuration, Control, and
   Inspection) is the SystemC standard for runtime configuration.
   The broker manages all parameters and allows them to be set via
   Lua or command-line arguments.

3. **Parse arguments** — `ArgParser` handles `--gs_luafile` (to load
   `platform.lua`) and `--param` (to override individual values).

4. **Instantiate the platform** — Creating `GreenSocsPlatform` triggers
   the `Container` to read the Lua file and instantiate all components.

5. **Run the simulation** — `sc_start()` runs until the guest firmware
   halts (via PSCI `SYSTEM_OFF`) or you press `Ctrl+\`.

---

## Building the Example

`hello.c` is a minimal bare-metal program that writes to the
PL011 data register at `0x09000000` one byte at a time, then
shuts down the virtual machine:

```c
#define UART_DR ((volatile unsigned int *)0x09000000)

/* PSCI function IDs (SMCCC compliant). */
#define PSCI_SYSTEM_OFF 0x84000008

void __attribute__((naked)) _start(void) {
    /* Set up a stack at the top of the 256 MB RAM region,
     * then branch to main. */
    __asm__ volatile(
        "ldr x0, =0x90000000\n"
        "mov sp, x0\n"
        "bl main\n"
        "1: b 1b\n"
    );
}

static inline void psci_system_off(void) {
    register unsigned long x0 __asm__("x0")
        = PSCI_SYSTEM_OFF;
    __asm__ volatile("hvc #0" : : "r"(x0));
    __builtin_unreachable();
}

void main(void) {
    const char *msg = "Hello from Qbox!\r\n";
    while (*msg)
        *UART_DR = *msg++;
    psci_system_off();
}
```

`_start` is marked `naked` so the compiler does not generate a
function prologue. This matters because the AArch64 CPU's stack
pointer is undefined at reset — the very first thing the code
must do is set SP to a valid address. The inline assembly
points it at `0x90000000` (the top of the 256 MB RAM region)
and then branches to `main`, which can safely use the stack.

### Ending the simulation

There are two ways to stop a Qbox simulation:

1. **From the firmware (automatic).** The firmware calls PSCI
`SYSTEM_OFF` (`0x84000008`) via an `hvc` instruction. QEMU
handles this as a system shutdown request, and the simulation
ends cleanly. This works because `platform.lua` configures the
CPU with `psci_conduit = "hvc"`, which tells QEMU to intercept
HVC calls and interpret them as PSCI commands.

2. **From the terminal (manual).** Press `Ctrl+\` (sends SIGQUIT)
to terminate the simulation. This is the standard way to stop
a Qbox platform that runs indefinitely, such as one booting
Linux. If the firmware has no shutdown path (for example,
ending with `for (;;);` instead of `psci_system_off()`), this
is the only way to stop it.

### Build the VP and firmware

First, build and install the main Qbox project. From the Qbox
root directory:

```bash
cmake -B build . \
    -DCMAKE_INSTALL_PREFIX=$(pwd)/build/install \
    -DLIBQEMU_TARGETS=aarch64
cmake --build build --parallel $(nproc)
cmake --install build
```

`-DLIBQEMU_TARGETS=aarch64` tells the build to compile in
the path to the AArch64 QEMU library (`libqemu-system-
aarch64.so`). Without it, the VP fails at runtime with
`target 'AArch64' disabled at compile time`.

This installs the Qbox CMake package that the standalone example
finds with `find_package(qbox)`.

Then, from this directory, configure and build the standalone
example against that install:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=<path-to-qbox-install-prefix>
cmake --build build
```

`-DCMAKE_PREFIX_PATH` must point to the Qbox install prefix from
the first step. For example, if you used `$(pwd)/build/install`
above, it is `../../build/install` from this directory. The
`hello-qbox-vp` binary is produced in this directory's `build/`
subdirectory, not under the Qbox install tree.

If `aarch64-linux-gnu-gcc` is on your `PATH`, the build also
cross-compiles `hello.elf` into this directory's `build/`
subdirectory, which is where `platform.lua` expects to find
it. If the cross-compiler is not available, a warning is
printed and you can build the firmware manually (see below).

### Manual firmware build

The example's CMake build produces both the VP and `hello.elf`
together. If `aarch64-linux-gnu-gcc` was not on your `PATH` when
you built the example, so `hello.elf` was not generated, use the
cross-compiler command below to build only the firmware without
reconfiguring or rebuilding the whole example:

```bash
mkdir -p build
aarch64-linux-gnu-gcc -nostdlib -static \
    -Ttext=0x80000000 -e _start \
    -Wl,--build-id=none -Wl,-n \
    -o build/hello.elf hello.c
```

The linker flags explained:

- `-Ttext=0x80000000` places the `.text` section at the base
  of the RAM region defined in `platform.lua`.
- `-Wl,--build-id=none` suppresses the `.note.gnu.build-id`
  section, which the linker would otherwise place in a
  separate LOAD segment at a low address outside the RAM
  region.
- `-Wl,-n` turns off page-alignment of ELF segments. Without
  it, the linker pads the LOAD segment to a 64 KB boundary,
  pushing its start address below `0x80000000` and out of the
  mapped RAM.

Together, these flags produce a single LOAD segment that
starts exactly at `0x80000000`, fitting entirely within the
memory configured in `platform.lua`.

---

## Running Your Platform

From this directory:

```bash
./build/hello-qbox-vp --gs_luafile platform.lua
```

Expected output in your terminal:

```
Hello from Qbox!
```

The message appears directly in the terminal because `platform.lua`
connects the PL011 to a `char_backend_stdio` backend. Every byte the
guest writes to `0x09000000` is forwarded to the host process's
stdout — no separate telnet connection needed.

Override individual parameters at the command line without editing
the file:

```bash
# Change log level
./build/hello-qbox-vp --gs_luafile platform.lua \
    --param platform.log_level=4

# Override the UART backend to a TCP socket instead of stdio
./build/hello-qbox-vp --gs_luafile platform.lua \
    --param 'platform.pl011_uart_0.backend.address="127.0.0.1:4001"'

# Then connect with: telnet localhost 4001
```

To list all available parameters for a platform:

```bash
./build/hello-qbox-vp --gs_luafile platform.lua --debug-all
```

---

## Debugging with GDB

Qbox exposes a GDB server for each CPU. Set `gdb_port` on a CPU
component, then connect with your GDB client.

In `platform.lua`:

```lua
platform.cpu_0 = {
    -- ... existing config ...
    gdb_port = 4321,
}
```

Or override it at runtime without editing the file:

```bash
./build/hello-qbox-vp --gs_luafile platform.lua \
    --param platform.cpu_0.gdb_port=4321
```

Connect from another terminal:

```bash
aarch64-linux-gnu-gdb build/hello.elf
(gdb) target remote localhost:4321
(gdb) continue
```

The platform starts paused when a GDB port is configured. It waits
for a debugger to connect before executing the first instruction.

To access the QEMU monitor (useful for inspecting device state,
injecting interrupts, etc.):

```bash
./build/hello-qbox-vp --gs_luafile platform.lua \
    --param \
    'platform.qemu_inst.qemu_args.-monitor="tcp:127.0.0.1:55555,server,nowait"'

# Connect with:
telnet localhost 55555
```

---

## Adding More Peripherals

### Interrupt controller (GIC)

The hello-qbox platform uses polled UART output and does not
need an interrupt controller. Any platform that uses interrupts
— timer-driven scheduling, interrupt-driven UART RX, virtio
devices — needs a GICv3:

```lua
platform.gic_0 = {
    moduletype = "arm_gicv3",
    args       = { "&qemu_inst" },

    -- Distributor interface (GICD)
    dist_iface = {
        address = 0x08000000,
        size    = 0x10000,
        bind    = "&router.initiator_socket",
    },

    -- Redistributor interface (GICR), one per CPU
    redist_iface_0 = {
        address = 0x080A0000,
        size    = 0x20000,
        bind    = "&router.initiator_socket",
    },

    num_cpus = 1,
    num_spi  = 64,
}
```

Connect the CPU to the interrupt controller:

```lua
platform.cpu_0.irq_out = { bind = "&gic_0.irq_in" }
```

### Second UART (TCP socket backend)

A socket backend lets you connect via telnet rather than using the
terminal directly:

```lua
platform.charbackend_socket_1 = {
    moduletype = "char_backend_socket",
    address    = "127.0.0.1:4002",
    server     = true,
}

platform.pl011_uart_1 = {
    moduletype    = "Pl011",
    dylib_path    = "uart-pl011",
    target_socket = {
        address = 0x09001000,
        size    = 0x1000,
        bind    = "&router.initiator_socket",
    },
    backend_socket = {
        bind = "&charbackend_socket_1.biflow_socket" },
    irq = { bind = "&gic_0.spi_in_6" },
}
```

### Virtual network interface

```lua
platform.virtionet_0 = {
    moduletype = "virtio_mmio_net",
    args       = { "&qemu_inst" },
    mem        = {
        address = 0x0a000000,
        size    = 0x200,
        bind    = "&router.initiator_socket",
    },
    irq        = { bind = "&gic_0.spi_in_16" },
    -- Forward host port 2222 to guest port 22 (SSH)
    netdev_str = "type=user,hostfwd=tcp::2222-:22",
}
```

### Virtual block device (disk image)

```lua
platform.virtioblk_0 = {
    moduletype = "virtio_mmio_blk",
    args       = { "&qemu_inst" },
    mem        = {
        address = 0x0a000200,
        size    = 0x200,
        bind    = "&router.initiator_socket",
    },
    irq        = { bind = "&gic_0.spi_in_17" },
    blkdev_str = "file=rootfs.img,format=raw,if=none",
}
```

### Multiple CPUs

Add extra CPUs by duplicating the CPU table. Each CPU needs its own
redistributor interface in the GIC.

```lua
platform.cpu_1 = {
    moduletype   = "cpu_arm_cortexA53",
    args         = { "&qemu_inst" },
    mem          = { bind = "&router.target_socket" },
    has_el3      = true,
    has_el2      = true,
    psci_conduit = "hvc",
    irq_out      = { bind = "&gic_0.irq_in_1" },
}

-- Add a second redistributor to gic_0
platform.gic_0.redist_iface_1 = {
    address = 0x080C0000,
    size    = 0x20000,
    bind    = "&router.initiator_socket",
}
platform.gic_0.num_cpus = 2
```

---

## Next Steps

- **Look at the example platforms** in `platforms/ubuntu/` — these
  are complete, booting Linux configurations for AArch64 and RISCV64.
- **Read the component headers** in `systemc-components/` and
  `qemu-components/` to see every available parameter for each
  component.
- **Enable KVM acceleration** by setting `accel = "kvm"` in
  `qemu_inst` when running on native AArch64 Linux hardware for
  near-native simulation speed.
- **Trace instruction execution** for debugging by passing QEMU
  trace flags:
  ```bash
  --param 'platform.qemu_inst.qemu_args.-d="in_asm"'
  ```
