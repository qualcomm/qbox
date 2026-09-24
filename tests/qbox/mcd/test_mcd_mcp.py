#!/usr/bin/env python3
# Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Integration test for the mcd_mcp MCP server talking to mcd_server inside the
# hello-qbox virtual platform. Checks:
#   1.  connect / status (cores, mem_spaces)
#   2.  write_mem / read_mem round-trip  (TLM transport_dbg)
#   3.  second distinct write/read pattern (no stale-data false pass)
#   4.  stop
#   5.  read_reg – PC (reg 32, AArch64 gdbstub layout)
#   6.  single step – PC must advance
#   7.  write_reg / read_reg round-trip on x1 (reg 1)
#   8.  snapshot / diff
#   9.  regs_dump
#   10. breakpoint: set / list / run / wait_stop hit / clear
#   11. write watchpoint: set / run / wait_stop hit / clear
#   12. run
#   13. describe
#
# Arguments (passed by CTest via CMakeLists.txt):
#   -e / --exe   path to the virtual-platform binary (mcd-vp)
#   -l / --lua   path to mcd-platform.lua
#   -m / --mcp   path to the mcd_mcp binary
#   -f / --fw    path to the debug firmware ELF (passed to the lua as fw)

import argparse
import json
import os
import signal
import socket
import subprocess
import sys
import time


def find_free_port() -> int:
    """Bind to port 0 and let the OS pick; return that port."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def wait_for_port(host: str, port: int, timeout: float = 10.0) -> None:
    """Poll until a TCP port is accepting connections, or raise TimeoutError."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.1)
    raise TimeoutError(f"port {host}:{port} did not open within {timeout}s")


def parse_u64_from_text(text: str) -> int:
    """Parse the 0x-prefixed hex value returned by mcd_read_reg."""
    return int(text.strip(), 16)


class MCP:
    """Thin wrapper around an mcd_mcp subprocess using JSON-RPC 2.0 stdio."""

    def __init__(self, mcp_exe: str) -> None:
        self._proc = subprocess.Popen(
            [mcp_exe],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self._id = 0
        self._call("initialize", {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {"name": "test", "version": "0"},
        })
        self._notify("notifications/initialized")

    def _call(self, method: str, params: dict) -> dict:
        self._id += 1
        req = json.dumps({"jsonrpc": "2.0", "id": self._id,
                          "method": method, "params": params})
        self._proc.stdin.write(req + "\n")
        self._proc.stdin.flush()
        line = self._proc.stdout.readline()
        return json.loads(line)

    def _notify(self, method: str) -> None:
        msg = json.dumps({"jsonrpc": "2.0", "method": method})
        self._proc.stdin.write(msg + "\n")
        self._proc.stdin.flush()

    def tool(self, name: str, args: dict) -> str:
        """Call a tool; return the text result. Raises on isError=true."""
        resp = self._call("tools/call", {"name": name, "arguments": args})
        if "error" in resp:
            raise RuntimeError(f"RPC error calling {name!r}: {resp['error']}")
        result = resp["result"]
        text = result["content"][0]["text"]
        if result.get("isError"):
            raise RuntimeError(f"tool {name!r} returned error: {text}")
        return text

    def close(self) -> None:
        try:
            self._proc.stdin.close()
        except Exception:
            pass
        self._proc.terminate()
        self._proc.wait(timeout=5)


def run_test(vp_exe: str, lua_file: str, mcp_exe: str, fw: str) -> None:
    mcd_port = find_free_port()

    print(f"mcd_port={mcd_port}")

    # Port and firmware path are CCI presets, which must precede --gs_luafile to
    # be visible to the lua's GET(). "-p name=value" is parsed with
    # cci_value::from_json, so a string value must be JSON-quoted. No gdb_port is
    # passed: mcd_server assigns a free one per QEMU instance (the stub is per
    # instance, not per CPU).
    cmd = [vp_exe, "-p", f"mcd.port={mcd_port}"]
    if fw:
        cmd += ["-p", f'fw="{fw}"']
    cmd += ["--gs_luafile", lua_file]
    vp = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=dict(os.environ),
    )

    mcp = None
    try:
        # mcd_server opens its TCP port at end_of_elaboration.
        wait_for_port("127.0.0.1", mcd_port, timeout=20.0)

        mcp = MCP(mcp_exe)

        # 1. Connect / status
        result = mcp.tool("mcd_connect", {"host": "127.0.0.1", "port": mcd_port})
        print(f"[1] connect: {result}")
        assert "connected" in result, f"unexpected connect result: {result!r}"

        status = json.loads(mcp.tool("mcd_status", {}))
        print(f"[1] status: {json.dumps(status, indent=2)}")
        assert status["connected"], "status should show connected"
        assert len(status["cores"]) >= 1, "expected at least one core"
        assert len(status["mem_spaces"]) >= 1, "expected at least one mem_space"

        # 2 & 3. Write / read-back two distinct patterns in scratch RAM, near the
        # top of the 256 MB region (0x80000000..0x90000000), clear of the firmware.
        for scratch, pattern in [
            ("8ffffff0", "deadbeef01020304"),
            ("8fffffe0", "0011223344556677"),
        ]:
            mcp.tool("mcd_write_mem", {"addr": scratch, "data": pattern})
            readback = mcp.tool("mcd_read_mem", {"addr": scratch, "len": 8})
            flat = "".join(readback.split()).lower()
            expected = "".join(pattern[i:i+2] for i in range(0, len(pattern), 2))
            assert expected in flat, (
                f"mem round-trip @ {scratch}: expected {expected!r} in {flat!r}"
            )
            print(f"[2/3] mem @ 0x{scratch}: OK")

        # 4. Stop
        result = mcp.tool("mcd_stop", {})
        print(f"[4] stop: {result}")
        assert result == "ok", f"mcd_stop returned unexpected: {result!r}"

        # 5. Read PC (reg 32 in the AArch64 gdbstub layout)
        PC_REG = 32
        pc1 = parse_u64_from_text(mcp.tool("mcd_read_reg", {"regno": PC_REG}))
        print(f"[5] PC before step: 0x{pc1:016x}")
        assert pc1 != 0, "PC should not be zero after reset"

        # 6. Single step - PC must advance
        mcp.tool("mcd_step", {"cpu_idx": 0})
        pc2 = parse_u64_from_text(mcp.tool("mcd_read_reg", {"regno": PC_REG}))
        print(f"[6] PC after  step: 0x{pc2:016x}")
        assert pc2 != pc1, f"PC did not change after step (still 0x{pc1:016x})"

        # 7. Write / read-back a GP register (x1 = regno 1)
        CANARY = 0xA5A5A5A5DEADC0DE
        mcp.tool("mcd_write_reg", {"regno": 1, "value": f"0x{CANARY:016x}"})
        x1 = parse_u64_from_text(mcp.tool("mcd_read_reg", {"regno": 1}))
        print(f"[7] x1 read back: 0x{x1:016x}")
        assert x1 == CANARY, (
            f"register write/read-back mismatch: wrote 0x{CANARY:016x}, got 0x{x1:016x}"
        )

        # 8. Snapshot / diff: snapshot r0..r32 so the range includes the PC (reg
        # 32); stepping the firmware's idle loop changes PC but no GP register.
        mcp.tool("mcd_snapshot", {"name": "before_step", "count": 33})
        mcp.tool("mcd_step", {"cpu_idx": 0})
        diff = mcp.tool("mcd_diff", {"name": "before_step"})
        print(f"[8] diff:\n{diff[:400]}")
        assert "changed" in diff.lower() or "r32" in diff.lower(), (
            f"diff after step should report changed registers; got: {diff!r}"
        )

        # 9. Regs dump
        dump = mcp.tool("mcd_regs_dump", {"cpu_idx": 0})
        print(f"[9] regs_dump (first 400 chars):\n{dump[:400]}")
        assert "r0" in dump.lower(), "regs_dump should mention r0"

        # 10. Breakpoint: halt, note the PC, step to a second loop address, then
        # arm the breakpoint at the *first* address, so continuing executes at
        # least one instruction before trapping back at bp_addr.
        mcp.tool("mcd_stop", {})
        bp_addr = parse_u64_from_text(mcp.tool("mcd_read_reg", {"regno": PC_REG}))
        mcp.tool("mcd_step", {"cpu_idx": 0})
        stepped = parse_u64_from_text(mcp.tool("mcd_read_reg", {"regno": PC_REG}))
        assert stepped != bp_addr, "expected the step to move PC within the loop"

        set_msg = mcp.tool("mcd_set_bp", {"addr": f"{bp_addr:x}", "type": "sw"})
        print(f"[10] set_bp: {set_msg}")

        listing = mcp.tool("mcd_list_bp", {})
        print(f"[10] list_bp:\n{listing}")
        assert f"{bp_addr:x}" in listing.lower(), f"bp not listed: {listing!r}"

        mcp.tool("mcd_run", {})
        stop = mcp.tool("mcd_wait_stop", {"timeout_ms": 5000})
        print(f"[10] wait_stop: {stop}")
        assert "breakpoint" in stop, f"expected a breakpoint stop, got: {stop!r}"
        pc_hit = parse_u64_from_text(mcp.tool("mcd_read_reg", {"regno": PC_REG}))
        assert pc_hit == bp_addr, (
            f"breakpoint hit at 0x{pc_hit:016x}, expected 0x{bp_addr:016x}"
        )

        mcp.tool("mcd_clear_bp", {"addr": f"{bp_addr:x}", "type": "sw"})
        listing = mcp.tool("mcd_list_bp", {})
        assert f"{bp_addr:x}" not in listing.lower(), f"bp not cleared: {listing!r}"
        print("[10] breakpoint set/hit/clear: OK")

        # 11. Write watchpoint: hello-debug.c bumps a counter at 0x8ff00000 on
        # every loop iteration.
        COUNTER = 0x8ff00000
        wp_msg = mcp.tool("mcd_set_bp",
                          {"addr": f"{COUNTER:x}", "type": "write", "len": 8})
        print(f"[11] set write-watchpoint: {wp_msg}")
        mcp.tool("mcd_run", {})
        wstop = mcp.tool("mcd_wait_stop", {"timeout_ms": 5000})
        print(f"[11] wait_stop: {wstop}")
        assert "watchpoint" in wstop, f"expected a watchpoint stop, got: {wstop!r}"
        mcp.tool("mcd_clear_bp", {"addr": f"{COUNTER:x}", "type": "write", "len": 8})
        print("[11] write watchpoint: OK")

        # 12. Resume
        result = mcp.tool("mcd_run", {})
        print(f"[12] run: {result}")
        assert result == "ok", f"mcd_run returned unexpected: {result!r}"

        # 13. Describe
        desc = mcp.tool("mcd_describe", {})
        print(f"[11] describe:\n{desc}")
        assert "host:" in desc, "describe output missing 'host:'"
        assert "mem_spaces" in desc, "describe output missing 'mem_spaces'"

        print("\nALL ASSERTIONS PASSED")

    finally:
        if mcp:
            mcp.close()
        vp.send_signal(signal.SIGTERM)
        try:
            out, _ = vp.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            vp.kill()
            out, _ = vp.communicate()
        if out:
            print("--- VP stdout ---")
            print(out[-3000:])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("-e", "--exe", required=True,
                        metavar="VP", help="Path to the virtual-platform binary")
    parser.add_argument("-l", "--lua", required=True,
                        metavar="LUA", help="Path to mcd-platform.lua")
    parser.add_argument("-m", "--mcp", required=True,
                        metavar="MCP", help="Path to mcd_mcp binary")
    parser.add_argument("-f", "--fw", default="",
                        metavar="FW", help="Path to the debug firmware ELF")
    args = parser.parse_args()

    required = [args.exe, args.lua, args.mcp]
    if args.fw:
        required.append(args.fw)
    for path in required:
        if not os.path.isfile(path):
            print(f"ERROR: file not found: {path}", file=sys.stderr)
            return 1

    run_test(args.exe, args.lua, args.mcp, args.fw)
    return 0


if __name__ == "__main__":
    sys.exit(main())
