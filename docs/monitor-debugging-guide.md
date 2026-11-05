# QBox Monitor — CLI Debugging Guide

The QBox monitor exposes REST and WebSocket endpoints that can be used
from the command line for live debugging of a running simulation. This
is particularly useful when diagnosing hangs, interrupt storms, or
unexpected CPU states without needing the QEMU monitor window.

## Prerequisites

- A running simulation with the monitor module enabled
- `curl` (REST endpoints)
- `websocat` (WebSocket/QMP commands). On macOS, install it
  with `brew install websocat`.

The examples use monitor port **18088**. This is an example;
use the port set by the `server_port` CCI parameter in your
platform.

The monitor provides `/sc_time`, `/pause`, `/continue`,
`/object/`, `/object/<str>`, `/qk_status`,
`/mcips_plugin_status`, `/sc_suspended`, `/refresh_interval`,
`/transport_dbg/<int>/<str>`, `/biflows`, and
`/biflow/<str>`. The examples below use only some of these routes.

---

## 1. Quick Health Check

```bash
# Current simulation time (is it advancing?)
curl -s http://localhost:18088/sc_time
# Example output; the value is SystemC time, not wall-clock seconds.
# {"sc_time_stamp":516.179}

# Check again after a couple of seconds
sleep 2 && curl -s http://localhost:18088/sc_time
```

Compare samples over time. If `sc_time_stamp` is not advancing,
the SystemC simulation time is not advancing. Sample values and
intervals in this guide are illustrative, not timing guarantees.

---

## 2. CPU State — Quantum-Keeper Status

```bash
# What states does the QK debug endpoint report?
curl -s http://localhost:18088/qk_status | python3 -c "
import json, sys
for qk in json.load(sys.stdin):
    if 'cpu' in qk['name']:
        print(f\"{qk['name']:50s} {qk['state']:10s} local={qk['local_time']}\")
"
```

Example output:
```
# Example output only. CPU names and values depend on the platform.
platform.cpu_0.qk          RUNNING    local=1007631952
platform.cpu_1.qk          IDLE       local=1007631862
...
```

- `RUNNING`, `IDLE`, and `NONE` are quantum-keeper worker states.
  They do not report guest CPU states such as WFI or WFE.

Do not infer guest instruction execution or wait state from these
worker states alone.

---

## 3. Inspecting SystemC Objects and CCI Parameters

```bash
# List all top-level objects
curl -s http://localhost:18088/object/ | python3 -m json.tool

# Inspect a specific module (e.g. the GIC)
curl -s http://localhost:18088/object/platform.gic_0 | python3 -m json.tool

# Inspect a CPU
curl -s http://localhost:18088/object/platform.cpu_0 | python3 -m json.tool
```

The response can include `child_objects`, `cci_params`, and
`child_dbg_ports`.

---

## 4. Debug Transport Implementation Bug

The route shape is `/transport_dbg/<int>/<str>`: an integer
address followed by a socket name.

**Current implementation bug:** the handler parses the address,
but ignores it and sends a four-byte TLM debug transaction at
address 0. This is a defect, not intended API behavior. The
current implementation cannot read an arbitrary requested
address. Do not use this route as a memory-read API or assume it
reads a specific device or GIC register.

---

## 5. Listing Biflow Sockets

```bash
curl -s http://localhost:18088/biflows
# Example names only. Discover names on the running platform.
# {"biflows":["platform.backend_router_17.socket",
#             "platform.qmp.qmp_socket.qmp_socket_router",
#             "platform.qmp_json.qmp_socket.qmp_socket_router"]}
```

The `qmp_json` name shown here is from a Dragonwing
`auto_gen4` configuration example. Biflow names depend on the
platform; use `/biflows` and select a name present in its output.

---

## 6. Sending QEMU Monitor Commands via QMP

This example sends a QEMU human monitor command through a QMP
WebSocket. The available biflow name and QEMU commands depend on
the platform and its QEMU instance.

### Basic pattern

```bash
echo '{"execute": "human-monitor-command", "arguments": {"command-line": "CMD"}}' \
  | timeout 3 websocat -n1 \
    ws://localhost:18088/biflow/platform.qmp_json.qmp_socket.qmp_socket_router
```

> **Note:** Biflow connections replay up to 1000 bytes from a
> retained `std::string` buffer to new connections. Parse
> responses carefully; you may see old data before your result.

### Get CPU register state

```bash
# Registers for the currently selected CPU. CPU selection and
# guest CPU indices depend on the platform.
echo '{"execute": "human-monitor-command", "arguments": {"command-line": "info registers"}}' \
  | timeout 3 websocat -n1 \
    ws://localhost:18088/biflow/platform.qmp_json.qmp_socket.qmp_socket_router \
  2>&1 | python3 -c "
import sys, re, json
raw = sys.stdin.read()
returns = re.findall(r'\"return\":\s*\"((?:[^\"\\\\\"]|\\\\.)*)\"', raw)
for r in returns:
    decoded = r.replace(r'\r\n', '\n')
    if 'PC=' in decoded:
        print(decoded)
"
```

### Switch CPU and read registers

The CPU index `4` below is an example. Guest CPU indices and QMP
output depend on the platform; use `info cpus` to inspect available
CPUs.

```bash
(sleep 0.3
 echo '{"execute": "human-monitor-command", "arguments": {"command-line": "cpu 4"}}'
 sleep 0.3
 echo '{"execute": "human-monitor-command", "arguments": {"command-line": "info registers"}}'
 sleep 1
) | timeout 5 websocat \
    ws://localhost:18088/biflow/platform.qmp_json.qmp_socket.qmp_socket_router \
  2>&1 > /tmp/qmp_regs.txt
```

### Read physical memory (platform example)

```bash
# Example physical address only; memory layout depends on the platform.
echo '{"execute": "human-monitor-command", "arguments": {"command-line": "xp /4xw 0x17A00000"}}' \
  | timeout 3 websocat -n1 \
    ws://localhost:18088/biflow/platform.qmp_json.qmp_socket.qmp_socket_router
```

### Read virtual memory (address and CPU context are platform dependent)

```bash
echo '{"execute": "human-monitor-command", "arguments": {"command-line": "x /4xw 0xffffff80600b8000"}}' \
  | timeout 3 websocat -n1 \
    ws://localhost:18088/biflow/platform.qmp_json.qmp_socket.qmp_socket_router
```

### Enable/disable QEMU trace logging

```bash
# Enable interrupt logging
echo '{"execute": "human-monitor-command", "arguments": {"command-line": "log int"}}' \
  | timeout 3 websocat -n1 \
    ws://localhost:18088/biflow/platform.qmp_json.qmp_socket.qmp_socket_router

# Disable logging
echo '{"execute": "human-monitor-command", "arguments": {"command-line": "log none"}}' \
  | timeout 3 websocat -n1 \
    ws://localhost:18088/biflow/platform.qmp_json.qmp_socket.qmp_socket_router
```

### List all CPUs

```bash
echo '{"execute": "human-monitor-command", "arguments": {"command-line": "info cpus"}}' \
  | timeout 3 websocat -n1 \
    ws://localhost:18088/biflow/platform.qmp_json.qmp_socket.qmp_socket_router
```

---

## 7. Useful One-Liners

### Is the system making progress?

```bash
for i in 1 2 3; do
  curl -s http://localhost:18088/sc_time
  echo
  sleep 2
done
```

### Sample a CPU PC repeatedly (platform example)

```bash
for i in $(seq 1 5); do
  echo '{"execute": "human-monitor-command", "arguments": {"command-line": "info registers"}}' \
    | timeout 3 websocat -n1 \
      ws://localhost:18088/biflow/platform.qmp_json.qmp_socket.qmp_socket_router \
    2>&1 | grep -oE 'PC=[0-9a-f]+' | tail -1
  sleep 1
done
```

### GIC distributor dump via QMP (platform example)

```bash
# Example address and register offsets only; verify them for the
# platform. This does not establish a general GIC address.
for i in $(seq 0 13); do
  off=$((0x200 + i * 4))
  hex=$(printf "0x17A00%03x" $off)  # adjust base address
  echo '{"execute":"human-monitor-command","arguments":{"command-line":"xp /1xw '$hex'"}}' \
    | timeout 3 websocat -n1 \
      ws://localhost:18088/biflow/platform.qmp_json.qmp_socket.qmp_socket_router \
    2>&1 | grep -oE '0x[0-9a-f]+'
done
```

---

## 8. Web Dashboard

Open `http://localhost:18088/` in a browser (adjust the port to
match `server_port`) for:

- **Object hierarchy browser** — click through modules, view CCI parameters
- **Terminal emulator** — connect to serial biflows (xterm.js)
- **VNC client** — connect to QEMU display (noVNC)
- **QMP command interface** — send commands interactively

---

## 9. Warnings

- **Pause and continue** — `/pause` suspends and `/continue`
  unsuspends the simulation. These endpoints do not guarantee
  guest-level pause or resume semantics.
- **Biflow buffer replay** — WebSocket connections replay up to
  1000 bytes from a retained `std::string` buffer. Parse
  responses carefully to distinguish old data from fresh
  responses.
- **Debug transport bug** — `/transport_dbg/<int>/<str>` ignores
  the parsed address and currently sends a four-byte transaction
  at address 0. This is a defect, not intended API behavior; it
  is not an arbitrary-address read API.
