<!--
Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
SPDX-License-Identifier: BSD-3-Clause
-->

# MCD debugging for QBox, with an MCP bridge for AI agents

This directory lets a tool — a script, a debugger front-end, or an AI agent such
as Claude — inspect and control a running QBox virtual platform through the
**MCD** (Multi-Core Debug) object model.

| Component | Location | Role |
|-----------|----------|------|
| `mcd_server` | `systemc-components/mcd_server` | SystemC dynamic module linked into the platform. Exposes an MCD object model over a length-prefixed binary TCP protocol. |
| `mcd_mcp` | `systemc-components/mcd_mcp` | Standalone process speaking the [Model Context Protocol](https://modelcontextprotocol.io) (JSON-RPC 2.0 over stdio). Connects to `mcd_server` and re-exposes debugging as MCP *tools*. |

```
   AI agent / MCP client            mcd_mcp                 mcd_server (in the VP)
  ───────────────────────   stdio  ─────────  TCP (binary)  ──────────────────────
   tools/call mcd_read_mem  ─────▶  mcd_client ───────────▶  transport_dbg ─▶ router/RAM
   tools/call mcd_read_reg  ─────▶            ───────────▶  GDB-RSP  ───────▶ QEMU gdbstub
```

## Capabilities

Memory accesses are TLM `transport_dbg` transactions, so they work whether or not
the CPU is running and never perturb timing. Registers, run control and
breakpoints/watchpoints go to QEMU's GDB stub (`Z`/`z` packets), so register
numbering follows the target's GDB layout (AArch64: `x0..x30` = 0..30, `sp` = 31,
`pc` = 32) and watchpoints fire only on CPU accesses, not on `mcd_write_mem`.
`mcd_server` enables the stub automatically: any QEMU instance whose `gdb_port`
CCI parameter is still 0 gets a free port assigned.

QEMU's stub state is global to an instance, so cores are per-instance GDB
threads: registers and stepping are per core, but run, stop and breakpoints act
on the whole instance. A breakpoint fires on whichever core of the instance
reaches the address first, and `mcd_wait_stop` reports the stop only on the core
QEMU attributed it to — poll each core when you do not know which will hit.

`mcd_server` holds the SystemC kernel open only while it is itself the reason the
CPUs are idle (a CPU it halted, or a breakpoint armed), releasing that hold on
resume, on clearing the last breakpoint, and on disconnect — which also resumes
the target and removes installed breakpoints. Natural quiescence and a firmware
power-off still end the simulation; platforms wanting it pinned open
unconditionally can instantiate `keep_alive`.

The MCD port grants unauthenticated read/write access to all of the simulated
machine's memory and registers, so the server binds to **loopback only**; reach
it from another host by tunnelling, not by rebinding.

## Building and running

Both components build as part of the normal QBox build, added from
`systemc-components/CMakeLists.txt` as `mcd_server` (`mcd_server.dylib`, loaded
by the platform) and `mcd_mcp` (which statically links the `mcd_client` and
`mcd_debug` helper libraries).

Instantiate `mcd_server` in your Lua platform description; see
`tests/qbox/mcd/mcd-platform.lua` for a complete example:

```lua
mcd_server = {
    moduletype = "mcd_server",
    dylib_path = "mcd_server",
    mcd_port   = 1235,        -- TCP port for the MCD wire protocol
}
```

During elaboration the server discovers routers (as memory spaces) and QEMU
instances (as debug endpoints, identified by the `tcg_mode` CCI parameter rather
than by C++ type), assigning each instance a `gdb_port`. Cores are discovered
from the GDB stub on first use.

## Registering the MCP server with an agent

`mcd_mcp` speaks MCP on stdio, so any MCP client can launch it. For Claude Code,
add it to your `.mcp.json` (or run `claude mcp add`):

```json
{
  "mcpServers": {
    "mcd": {
      "command": "/path/to/qbox/build/mcd_mcp"
    }
  }
}
```

Then, from the agent: `mcd_connect` with the `host`/`port` the platform is
listening on (default `127.0.0.1:1235`), `mcd_describe` to see the topology,
then `mcd_stop`, `mcd_read_reg`, `mcd_step`, `mcd_read_mem`, … to debug.

## Tools

| Tool | Purpose |
|------|---------|
| `mcd_connect` / `mcd_disconnect` | Open / close the connection to `mcd_server`. |
| `mcd_status` | JSON summary of the connection and object model. |
| `mcd_describe` | Human-readable topology table. |
| `mcd_refresh` | Re-query the target's object model. |
| `mcd_read_mem` / `mcd_write_mem` | Memory access (hex dump / hex byte string), optional `space_id`. |
| `mcd_read_reg` / `mcd_write_reg` | Register access by GDB register number, optional `cpu_idx`. |
| `mcd_regs_dump` | Dump a range of registers for a core. |
| `mcd_run` / `mcd_stop` / `mcd_step` | Run control. |
| `mcd_set_bp` / `mcd_clear_bp` / `mcd_list_bp` / `mcd_wait_stop` | Breakpoints and watchpoints. |
| `mcd_snapshot` / `mcd_diff` | Save a named register snapshot and report which registers changed since. |

## Tests

`tests/qbox/mcd` holds `test_mcd_unit.py` (MCP framing and tool schema, no
platform needed), `test_mcd_smp.py` (two CPUs on one `QemuInstance` via
`-p cores=2`) and `test_mcd_mcp.py` (end-to-end against the `mcd-vp`
platform). All are registered with CTest (`ctest -R mcd_mcp`); the integration
tests are skipped without an AArch64 toolchain (`aarch64-linux-gnu-gcc`, or
`clang` + `ld.lld`) to build their firmware.

The platform reads two CCI presets from the command line (via the lua `GET()`
global), which must appear before `--gs_luafile`: `-p mcd.port=<n>` and
`-p 'fw="<path>"'` (string values are JSON, so the path is quoted).
