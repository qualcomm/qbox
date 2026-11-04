#!/usr/bin/env python3
# Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Multi-core (SMP) integration test for mcd_server via mcd_mcp. Both CPUs share a
# single QemuInstance, whose gdb stub state is global and presents each vCPU as a
# gdb thread, so one gdb_port serves every core. Checks:
#   1. two cores are reported
#   2. registers are per core (distinct values written and read back)
#   3. stepping one core advances only that core
#   4. a breakpoint hit is reported
#   5. detaching resumes the target
#
# Arguments (passed by CTest via CMakeLists.txt):
#   -e / --exe   path to the virtual-platform binary (mcd-vp)
#   -l / --lua   path to mcd-platform.lua
#   -m / --mcp   path to the mcd_mcp binary
#   -f / --fw    path to the debug firmware ELF (passed to the lua as fw)

import argparse
import json
import os
import subprocess
import sys

# Both files live in this directory, which is sys.path[0] for a script run by path.
from test_mcd_mcp import MCP, find_free_port, wait_for_port, parse_u64_from_text

PC_REG = 32  # AArch64 gdbstub register layout: x0..x30 = 0..30, sp = 31, pc = 32

# The debug firmware's spin loop, from hello-debug.c compiled for AArch64: each
# step moves PC on by one 4-byte instruction and wraps at the end.
LOOP = [0x8000005C, 0x80000060, 0x80000064, 0x80000068, 0x8000006C, 0x80000070]


def read_pc(mcp: MCP, core: int) -> int:
    return parse_u64_from_text(mcp.tool("mcd_read_reg", {"regno": PC_REG, "cpu_idx": core}))


def run_test(vp_exe: str, lua_file: str, mcp_exe: str, fw: str) -> None:
    mcd_port = find_free_port()
    print(f"mcd_port={mcd_port} (2 cores)")

    # cores=2 makes the lua instantiate cpu_0 and cpu_1 on the one qemu_inst;
    # presets must precede --gs_luafile, and string values be JSON-quoted.
    cmd = [vp_exe, "-p", f"mcd.port={mcd_port}", "-p", "cores=2"]
    if fw:
        cmd += ["-p", f'fw="{fw}"']
    cmd += ["--gs_luafile", lua_file]
    vp = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, env=dict(os.environ))

    mcp = None
    try:
        wait_for_port("127.0.0.1", mcd_port, timeout=20.0)
        mcp = MCP(mcp_exe)
        mcp.tool("mcd_connect", {"host": "127.0.0.1", "port": mcd_port})

        # 1. Two cores, discovered from the gdb stub's thread list
        status = json.loads(mcp.tool("mcd_status", {}))
        cores = status["cores"]
        print(f"[1] cores: {json.dumps(cores)}")
        assert len(cores) == 2, f"expected 2 cores, got {len(cores)}: {cores}"

        mcp.tool("mcd_stop", {})

        # 2. Registers are per core: distinct values, so aliasing cannot pass.
        want = {0: 0xAAAA0000, 1: 0xBBBB1111}
        for core, val in want.items():
            mcp.tool("mcd_write_reg", {"regno": 0, "value": f"0x{val:016x}", "cpu_idx": core})
        for core, val in want.items():
            got = parse_u64_from_text(mcp.tool("mcd_read_reg", {"regno": 0, "cpu_idx": core}))
            print(f"[2] core {core} x0 = 0x{got:x}")
            assert got == val, f"core {core}: x0 is 0x{got:x}, expected 0x{val:x}"

        # 3. Stepping one core moves only that core, repeated and interleaved with
        # run/stop cycles so the 0x03 path is exercised too.
        steps = 24
        for k in range(steps):
            if k and k % 8 == 0:
                mcp.tool("mcd_run", {})
                mcp.tool("mcd_stop", {})

            before_1, before_0 = read_pc(mcp, 1), read_pc(mcp, 0)
            assert before_1 in LOOP, f"core 1 PC 0x{before_1:x} is outside the spin loop"

            mcp.tool("mcd_step", {"cpu_idx": 1})

            after_1, after_0 = read_pc(mcp, 1), read_pc(mcp, 0)
            expect_1 = LOOP[(LOOP.index(before_1) + 1) % len(LOOP)]
            assert after_1 == expect_1, (
                f"step {k}: core 1 PC 0x{before_1:x} -> 0x{after_1:x}, "
                f"expected 0x{expect_1:x}")
            assert after_0 == before_0, (
                f"step {k}: stepping core 1 moved core 0: "
                f"0x{before_0:x} -> 0x{after_0:x}")
        print(f"[3] {steps} single-core steps: core 1 advanced each time, core 0 never moved")

        # Step core 0 too, so only-core-1-is-steppable would be caught.
        before_0 = read_pc(mcp, 0)
        mcp.tool("mcd_step", {"cpu_idx": 0})
        after_0 = read_pc(mcp, 0)
        assert after_0 != before_0, f"core 0 did not step (PC stayed 0x{before_0:x})"
        print(f"[3] core 0 steps as well: 0x{before_0:x} -> 0x{after_0:x}")

        # 4. A breakpoint is hit and reported. QEMU arms Z/z per address space, so
        # whichever core arrives first wins; mcd_wait_stop reports a stop only to
        # its own core, so ask each in turn and assert only that the hit happened.
        bp_addr = 0x80000064
        mcp.tool("mcd_set_bp", {"addr": f"{bp_addr:x}", "type": "sw"})
        mcp.tool("mcd_run", {})

        hit = None
        for core in (0, 1):
            stop = mcp.tool("mcd_wait_stop", {"timeout_ms": 5000, "cpu_idx": core})
            print(f"[4] wait_stop core {core}: {stop}")
            if "still running" not in stop.lower():
                hit = (core, stop)
                break
        assert hit is not None, "neither core reported the breakpoint hit"
        print(f"[4] breakpoint reported by core {hit[0]}")
        mcp.tool("mcd_clear_bp", {"addr": f"{bp_addr:x}", "type": "sw"})

        # 5. Detaching resumes the target: closing the socket does not restart
        # QEMU's VM, so mcd_server must resume on detach or the platform exits.
        mcp.tool("mcd_stop", {})
        mcp.tool("mcd_disconnect", {})
        assert vp.poll() is None, "simulation exited after the debugger detached"

        # A fresh session must still work: the resume leaves QEMU owing a
        # stop-reply, which must not derail the next connection's handshake.
        mcp.tool("mcd_connect", {"host": "127.0.0.1", "port": mcd_port})
        status = json.loads(mcp.tool("mcd_status", {}))
        assert len(status["cores"]) == 2, \
            f"reconnect reported {len(status['cores'])} cores, expected 2"
        bps = mcp.tool("mcd_list_bp", {})
        print(f"[5] reconnected, cores=2, breakpoints after detach: {bps.strip()}")
        mcp.tool("mcd_run", {})

        print("SMP test: all checks passed")

    finally:
        if mcp is not None:
            mcp.close()
        vp.terminate()
        try:
            out, _ = vp.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            vp.kill()
            out, _ = vp.communicate()
        if out:
            print("--- VP output (tail) ---")
            print("\n".join(out.splitlines()[-40:]))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-e", "--exe", required=True)
    ap.add_argument("-l", "--lua", required=True)
    ap.add_argument("-m", "--mcp", required=True)
    ap.add_argument("-f", "--fw", default="")
    args = ap.parse_args()
    run_test(args.exe, args.lua, args.mcp, args.fw)
    return 0


if __name__ == "__main__":
    sys.exit(main())
