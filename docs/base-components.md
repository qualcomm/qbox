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
- Options: `address` (absolute) or `offset` (relative)
- Function:
  `void file_load(std::string filename, uint64_t addr, uint64_t file_offset = 0, uint64_t file_data_len = max)`

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
  `archived_file_name`, `archived_file_offset`,
  `archived_file_size`, `is_compressed`
- Function:
  `void zip_file_load(zip_t* p_archive, const std::string& archive_name, uint64_t addr, ...)`

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
  `<memoryname>.0x<start_addr>-0x<end_addr>.<outfile>`.
- `MemoryDumper_trigger` (`bool`): Writing to this parameter
  triggers the dump.

The dumper must be bound to the main system router. It
discovers all memories in the system, finds their addresses,
and requests data via its initiator port. A target port must
also be bound; accessing its bound address triggers the dump.

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
Targets with equal priority maintain their bind order
(`std::stable_sort`). Overlapping addresses are allowed --
the first match is used. The default priority is 0.

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

| Parameter | Type | Description |
|-----------|------|-------------|
| `p_mapped_base_addr` | `uint64_t` | Base address added to or subtracted from the incoming address to produce the outgoing address |

### Usage

Place the address translator on a path where the initiator
uses one address space and the target expects another. The
translator rewrites the transaction address using the
configured `p_mapped_base_addr`.

## Exclusive Monitor (exclusive_monitor)

The exclusive monitor implements the ARM exclusive access
protocol. It sits between an initiator and a target to track
exclusive load/store pairs, providing the semantics required
by ARM LDREX/STREX (and similar) instructions.

### Sockets

- `front_socket` -- target socket facing the initiator
- `back_socket` -- initiator socket facing the target

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

The DMI converter is a caching bridge that sits between
initiators and targets and caches Direct Memory Interface
(DMI) regions. By caching DMI pointers, it avoids repeated
DMI negotiations and improves simulation performance.

### CCI Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `p_tlm_ports_num` | `uint32_t` | Number of TLM port pairs to instantiate |

### Sockets

- `initiator_sockets` -- vector of initiator sockets (one
  per port)
- `target_sockets` -- vector of target sockets (one per
  port)

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
| `p_tlm_ports_num` | `uint32_t` | Number of TLM port pairs to instantiate |

### Sockets

- `initiator_sockets` -- vector of initiator sockets (one
  per port)
- `target_sockets` -- vector of target sockets (one per
  port)

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

| Parameter | Type | Description |
|-----------|------|-------------|
| `lazy_init` | `bool` | When true, defer initialization until first access |

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
| `p_RTquantum_ms` | `double` | Real-time quantum in milliseconds; the granularity at which the limiter checks and throttles simulation progress |
| `p_SCTimeout_ms` | `double` | SystemC timeout in milliseconds; maximum time the simulation may advance before yielding |
| `p_MaxTime_ms` | `double` | Maximum wall-clock time in milliseconds; stops the simulation after this duration |

### Sockets

None.

### Behavior

At each quantum boundary the limiter compares elapsed
simulation time against elapsed wall-clock time. If the
simulation is ahead, it sleeps until real time catches up.
The `p_MaxTime_ms` parameter can impose a hard wall-clock
cap on the simulation run.

## Exiter (exiter)

The exiter provides a mechanism to stop the simulation by
writing to a memory-mapped address. It exposes a single
target socket named `socket`.

### CCI Parameters

None.

### Sockets

- `socket` -- target socket; any write to this address
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
| `interval_us` | `uint64_t` | Interval in microseconds between successive time-stamp prints |

### Sockets

None.

### Behavior

At each configured interval, the time printer emits the
current simulation time to the log output. This provides a
quick visual indication of how far the simulation has
progressed and at what rate.

## PL011 UART (SystemC)

The PL011 UART is a pure SystemC model of the ARM PL011
Universal Asynchronous Receiver/Transmitter. It implements
the register interface, transmit and receive FIFOs, and
interrupt generation defined by the ARM PL011 specification.

### CCI Parameters

None.

### Sockets

- `socket` -- target socket exposing the PL011 register
  interface for memory-mapped access
- `backend_socket` -- biflow socket carrying the character
  stream to and from a backend (stdio, socket, or file)
- `irq` -- initiator signal socket for interrupt output

### Behavior

Guest software accesses the UART through its register
interface on `socket`. Characters written to the transmit
data register are forwarded through `backend_socket` to a
character backend. Characters arriving from the backend are
placed into the receive FIFO and, when enabled, trigger an
interrupt via `irq`. The model supports both polled and
interrupt-driven operation.

## Testing

Tests for each component are located under
`tests/base-components/`. After building, run:

```bash
# Run all tests
ctest --test-dir build -R base-components
```
