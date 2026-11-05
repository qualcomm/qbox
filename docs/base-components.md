# Base Components

The base-components library provides simple SystemC models
including routers, memories, loaders, and an exclusive monitor.
All components are "loosely timed" only, support DMI where
appropriate, and use CCI for configuration.

## Loader

The loader exposes a single initiator socket
(`initiator_socket`) which should be bound to the bus fabric
through which it loads memory contents. It supports the
following load types:

### ELF Files

- CCI parameter: `elf_file`
- Function: `void elf_load(const std::string& path)`
- Addresses are built into the ELF file; no address or offset
  parameter is available. If used within a memory context,
  addresses are offset within the memory.

### Binary Files

- CCI parameter: `bin_file`
- Options: `address` (absolute) or `offset` (relative),
  `bin_file_offset` and `bin_file_size` (used together to
  select a file range)
- Function:
  `void file_load(std::string& filename, uint64_t addr, uint64_t file_offset = 0, uint64_t file_data_len = std::numeric_limits<uint64_t>::max())`

### CSV Files (32-bit data)

- CCI parameter: `csv_file`
- Options: `address` (absolute) or `offset` (relative),
  `addr_str` (CSV column header for addresses), `value_str`
  (CSV column header for values), `byte_swap`
- Function:
  `void csv_load(std::string filename, uint64_t offset, std::string addr_str, std::string value_str, bool byte_swap)`

### ZIP Archives

- CCI parameter: `zip_archive`
- Options: `address` (absolute) or `offset` (relative),
  `archived_file_name`, `archived_file_offset`, and
  `archived_file_size`
- Function:
  `void zip_file_load(zip_t* p_archive, const std::string& archive_name, uint64_t addr, ...)`
- `is_compressed` is not a loader CCI parameter. It is an
  argument to the programmatic `zip_file_load` helper, which
  selects nested-archive extraction when true.

### String Parameter

- CCI parameter: `param`
- A `std::string` configuration parameter is loaded into
  memory. The parameter must be realized such that a typed
  handle can be obtained.
- Options: `address` (absolute) or `offset` (relative)
- Function: `void str_load(std::string data, uint64_t addr)`

### Device Tree (base DTB + overlays)

- CCI parameter: `dtb`
- Sub-parameters: `base` (base DTB file, required),
  `overlays` (ordered list of DTBO files, optional),
  `dump` (file receiving the final DTB, optional)
- Options: `address` (absolute) or `offset` (relative)
- Each overlay is applied to the result of the previous one
  with libfdt `fdt_overlay_apply` (the same operation as
  `fdtoverlay`); the result is packed, checked with
  `fdt_check_full`, and loaded at the given address.
- Overlays that reference labels need a base DTB compiled with
  symbols (`dtc -@`). Overlay sources are compiled with
  `dtc -@ -I dts -O dtb`.
- QBox links libfdt (`libfdt-dev` on Ubuntu, `dtc` on Homebrew/MSYS2).
- Function: `void dtb_load(const std::string& param, uint64_t addr)`

### Data Array (32-bit data)

- CCI parameter: `data`
- Options: `byte_swap`
- The data parameter is an array with numbered indices used
  as memory addresses.

### Raw Pointer

- No CCI parameter. Programmatic access only.
- Function: `void ptr_load(uint8_t *data, uint64_t addr, uint64_t len)`

### Example Configuration

```lua
loader = {
    {elf_file = "my_elf.elf"},
    {data = {0x1, 0x2}, address = 0x30},
    {dtb = {base = "board.dtb", overlays = {"overlay_a.dtbo", "overlay_b.dtbo"}},
     address = 0xAFD00000},
}
```

## Memory

The memory component provides a simple target socket named
`target_socket`.

### CCI Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `read_only` | `false` | Read-only memory |
| `dmi_allow` | `true` | Allow DMI access |
| `latency` | 10 NS | Latency reported for DMI access |
| `map_file` | none | File used to map this memory (persists across runs) |

### Size

The memory size is determined by (in order of precedence):
1. The size set by the configurator
2. The size given to the constructor
3. The size set on the target socket at bind time

### Built-in Loader

Each memory has a built-in loader. Configuration can be
applied to `memory_name.load`. In this case, all addresses
are treated as offsets.

## Memory Dumper

A debugging utility for dumping memory state to files.

- `outfile` (`std::string`): Output file name. The binary
  file is written as
  `<memoryname>.0x<start_addr>-0x<end_addr>.<outfile>`;
  `outfile` defaults to `dumpfile`. The end address is
  exclusive (`start_addr + memory_size`).
- `MemoryDumper_trigger` (`bool`): Writing to this parameter
  triggers the dump.

The dumper finds `gs_memory` objects in the SystemC hierarchy
and reads their configured `target_socket.address` and
`target_socket.size`. Bind `initiator_socket` to a path that
can reach those memories and grants DMI. The dumper stops
reading a memory when DMI is unavailable, so that output file
may contain only part of that memory. A TLM transaction at
address zero on `target_socket` also triggers a dump.

## Router

The router is a simple address-decoding device. Initiators
and targets bind directly to the router's `target_socket` and
`initiator_socket` (both are multi-sockets).

### Address Discovery

The router uses CCI parameters to discover target device
addresses:

- `<target_name>.<socket_name>.address`
- `<target_name>.<socket_name>.size`
- `<target_name>.<socket_name>.relative_addresses`
- `<target_name>.<socket_name>.priority`

The `relative_addresses` flag is a boolean -- targets that
opt in receive addresses relative to their base address. The
default is relative addressing.

### Routing Order

Targets are sorted by `priority` (lower values match first).
Overlapping addresses are allowed. If two targets have equal
priority in an overlap, the newly added mapping wins. The
default priority is 0.

### Convenience Functions

- `add_target(socket, base_address, size)` -- Sets
  appropriate CCI parameters (if not already set), sets
  `relative_addresses` to `true`, and handles socket binding.
- `add_initiator(socket)` -- Connects an initiator to the
  router and handles binding.

### PathIDExtension

The router adds a `gs::PathIDExtension` to transactions.
This extension holds a `std::vector` of port indices
(collectively a unique ID). The ID is composed by all routers
on the path that support this extension and can be used to
identify the originating initiator. The ID extension is held
in a thread-safe pool.

## Address Translator (addrtr)

The address translator sits between an initiator and a target
and applies a base address mapping to transactions passing
through it. It exposes a `target_socket` (for the upstream
initiator) and an `initiator_socket` (for the downstream
target).

### CCI Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `mapped_base_addr` | `uint64_t` | `0` | Base address used to translate addresses |

### Usage

Place the address translator on a path where the initiator
uses one address space and the target expects another. The
translator rewrites the transaction address using the
configured `mapped_base_addr`.

## Exclusive Monitor (exclusive_monitor)

The exclusive monitor implements the ARM exclusive access
protocol. It sits between an initiator and a target to track
exclusive load/store pairs, providing the semantics required
by ARM LDREX/STREX (and similar) instructions.

### Sockets

- C++ member `front_socket` is exposed for binding as
  `front-socket` and faces the upstream initiator
- C++ member `back_socket` is exposed for binding as
  `back-socket` and faces the downstream target

### CCI Parameters

None.

### Behavior

The monitor intercepts TLM transactions and tracks which
addresses have been exclusively loaded. A subsequent
exclusive store to the same address succeeds only if no
other initiator has written to it in the interim. This is
essential for implementing atomic read-modify-write
operations in multi-core ARM platforms.

## Pass-Through (pass)

The pass-through component is a transparent bridge between
an initiator and a target. It exposes an `initiator_socket`
and a `target_socket` and forwards all transactions without
modification.

### CCI Parameters

None.

### Sockets

- `initiator_socket` -- binds to the downstream target
- `target_socket` -- binds to the upstream initiator

### Usage

The pass component is useful for signal aliasing and
transaction forwarding scenarios where a transparent
intermediary is needed in the TLM binding topology without
altering the transaction payload.

## DMI Converter (dmi_converter)

The DMI converter is a bridge between initiators and targets
that caches granted Direct Memory Interface (DMI) regions.
It can serve eligible accesses through cached DMI pointers.
This is intended to reduce TLM traffic; actual performance
depends on the model and workload.

### CCI Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `dmic_tlm_ports_num` | `uint32_t` | Number of TLM port pairs to instantiate (default: 1) |

### Sockets

- C++ vector members: `initiator_sockets` and `target_sockets`.
  Bindable elements are named `initiator_socket_N` and
  `target_socket_N`, where `N` is the zero-based port index.

### Behavior

Each port pair acts as a pass-through that intercepts DMI
requests and caches the returned DMI regions. Subsequent
accesses that fall within a cached region are served directly
from the cached DMI pointer, bypassing the full TLM
transport path.

## TLM Bus Width Bridges (tlm_bus_width_bridges)

The bus width bridge converts transactions between TLM
sockets of different bus widths. This is necessary when an
initiator with one bus width must communicate with a target
that uses a different width.

### CCI Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `tlm_ports_num` | `uint32_t` | Number of TLM port pairs to instantiate (default: 1) |

### Sockets

- C++ vector members: `initiator_sockets` and `target_sockets`.
  Bindable elements are named `initiator_socket_N` and
  `target_socket_N`, where `N` is the zero-based port index.

### Behavior

The bridge transparently adapts the data width of
transactions so that components with mismatched TLM bus
widths can interoperate. Each port pair bridges one
initiator-target connection.

## Register Router (reg_router)

The register router is similar to the standard router but
adds support for register callback functions. It exposes an
`initiator_socket` and a `target_socket`.

### CCI Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `lazy_init` | `bool` | `false` | When true, defer initialization until first access |

### Sockets

- `initiator_socket` -- binds to the downstream target
- `target_socket` -- binds to the upstream initiator

### Behavior

The register router allows software-modeled registers to be
associated with callback functions that are invoked on read
or write access. This is useful for implementing device
register maps where side effects must occur on access, rather
than simple memory-mapped storage.

## Real-Time Limiter (realtimelimiter)

The real-time limiter synchronizes simulation time with
wall-clock time. It prevents the simulation from running
faster than real time, which is useful for interactive
sessions or when the virtual platform must pace itself
against external real-time events.

### CCI Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `RTquantum_ms` | `double` | Real-time quantum in milliseconds; default 100 |
| `SCTimeout_ms` | `double` | Stalled-simulation timeout in milliseconds; default 0 disables it |
| `MaxTime_ms` | `double` | Maximum simulation run time in milliseconds; default 0 means no limit |

### Sockets

None.

### Behavior

The limiter paces simulation time against elapsed wall-clock
time. `SCTimeout_ms` is checked only when simulation time is
stalled; it causes a fatal report if the requested run-ahead
exceeds the configured limit. `MaxTime_ms` limits elapsed
simulation time, not wall-clock run time.

## Exiter (exiter)

The exiter provides a mechanism to stop the simulation by
writing to a memory-mapped address. Its C++ socket member is
named `socket`; the bindable socket name is `target_socket`.

### CCI Parameters

None.

### Sockets

- `target_socket` -- target socket; a write to this address
  triggers simulation termination

### Behavior

When any write transaction is received on its target socket,
the exiter calls `sc_stop()` to end the simulation. This is
typically mapped to a known address so that guest software
can cleanly shut down the virtual platform.

## Keep Alive (keep_alive)

The keep-alive component prevents the SystemC simulation
from ending prematurely. It uses an `async_event` to keep
the simulation kernel active even when no other events are
pending.

### CCI Parameters

None.

### Sockets

None.

### Behavior

SystemC simulations terminate when there are no more pending
events. In platforms that rely on external or asynchronous
stimuli (for example, QEMU-driven execution), the
keep-alive component ensures the simulation does not exit
before those stimuli arrive.

## Time Printer (timeprinter)

The time printer periodically logs the current simulation
time. This is a simple diagnostic component useful for
monitoring simulation progress.

### CCI Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `interval_us` | `uint64_t` | Simulation-time wait interval in microseconds before each print (default: 1,000,000) |

### Sockets

None.

### Behavior

The time printer logs elapsed wall-clock time, then waits for
`interval_us` of simulation time before logging again. Its
report requires the module's log level to be at least 4.

## PL011 UART (SystemC)

The PL011 UART is a pure SystemC model of the ARM PL011
Universal Asynchronous Receiver/Transmitter. It implements
the register interface, receive FIFO, and interrupt
generation. Transmit data is sent directly to the backend.

### CCI Parameters

None.

### Sockets

- C++ member `socket` is exposed for binding as
  `target_socket` and carries the PL011 register interface
- `backend_socket` -- biflow socket carrying the character
  stream to and from a backend (stdio, socket, or file)
- `irq` -- initiator signal socket for interrupt output

### Behavior

Guest software accesses the UART through its register
interface on `target_socket`. A character written to the
transmit data register is enqueued directly on
`backend_socket`; it is not held in a transmit FIFO. Characters
arriving from the backend enter the receive FIFO and can
trigger an interrupt via `irq`.

## Testing

Component test sources are under `tests/components/`. List the
tests registered by the current build with:

```bash
ctest --test-dir build -N
```

No test results are implied by this documentation. See
[`tests/components/loader/src/README.md`](../tests/components/loader/src/README.md)
for the loader test fixture notes.
