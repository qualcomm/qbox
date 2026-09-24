#!/usr/bin/env python3
# Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Reset-stress test: many resets inside ONE simulation, hunting a reported
# "platform hangs after some number of resets" bug. Each iteration drives an MCD
# RESET (which forwards to QEMU's system_reset through reset_gpio) and then
# proves the platform is not wedged:
#   (a) PC is back at the reset vector 0x80000000
#   (b) after RUN, the firmware counter at 0x8ff00000 advances again
# Every iteration runs under a wall-clock timeout so a hang is reported as a
# hang rather than an infinite wait.
#
# Arguments:
#   -e / --exe      path to the virtual-platform binary
#   -l / --lua      path to reset-stress.lua
#   -m / --mcp      path to the mcd_mcp binary
#   -f / --fw       path to the debug firmware ELF (passed to the lua as fw)
#   -n / --resets   number of resets (default 200)
#   --cores         cores preset (default 1)
#   --timeout       per-iteration timeout in seconds (default 30)

import argparse
import concurrent.futures
import os
import signal
import subprocess
import sys
import time

# test_mcd_mcp.py lives in the sibling mcd/ test directory.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mcd"))
from test_mcd_mcp import MCP, find_free_port, wait_for_port, parse_u64_from_text  # noqa: E402

PC_REG = 32       # AArch64 gdbstub: x0..x30 = 0..30, sp = 31, pc = 32
RESET_VECTOR = 0x80000000
COUNTER = 0x8FF00000


def read_counter(mcp: MCP) -> int:
    """Read the firmware's 64-bit little-endian counter via mcd_read_mem."""
    dump = mcp.tool("mcd_read_mem", {"addr": f"{COUNTER:x}", "len": 8})
    # hex_dump line: "<addr>: b0 b1 ... <ascii>"
    body = dump.split(":", 1)[1]
    bytes_ = [int(t, 16) for t in body.split()[:8]]
    return int.from_bytes(bytes(bytes_), "little")


def one_reset(mcp: MCP) -> int:
    """One reset + liveness check. Returns the counter value observed running."""
    mcp.tool("mcd_reset", {})

    pc = parse_u64_from_text(mcp.tool("mcd_read_reg", {"regno": PC_REG}))
    assert pc == RESET_VECTOR, f"PC after reset is 0x{pc:016x}, expected 0x{RESET_VECTOR:08x}"

    mcp.tool("mcd_run", {})
    # Sample until two consecutive reads increase. A single decrease is expected:
    # the restarted firmware re-zeroes the counter, so a sample taken before that
    # can exceed the next one. Only a total lack of progress means wedged.
    prev = read_counter(mcp)
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        time.sleep(0.05)
        cur = read_counter(mcp)
        if cur > prev:
            return cur
        prev = cur
    raise AssertionError(f"counter stuck at {prev} after reset: platform wedged")


def run_test(args) -> None:
    mcd_port = find_free_port()
    print(f"mcd_port={mcd_port} cores={args.cores} resets={args.resets}")

    cmd = [args.exe, "-p", f"mcd.port={mcd_port}", "-p", f"cores={args.cores}"]
    if args.fw:
        cmd += ["-p", f'fw="{args.fw}"']
    cmd += ["--gs_luafile", args.lua]
    vp = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, env=dict(os.environ))

    mcp = None
    done = 0
    pool = concurrent.futures.ThreadPoolExecutor(max_workers=1)
    try:
        wait_for_port("127.0.0.1", mcd_port, timeout=20.0)
        mcp = MCP(args.mcp)
        mcp.tool("mcd_connect", {"host": "127.0.0.1", "port": mcd_port})

        for i in range(1, args.resets + 1):
            if vp.poll() is not None:
                raise AssertionError(
                    f"simulation exited after {done} resets (rc={vp.returncode})")

            fut = pool.submit(one_reset, mcp)
            try:
                counter = fut.result(timeout=args.timeout)
            except concurrent.futures.TimeoutError:
                alive = vp.poll() is None
                print(f"HANG after {done} resets: iteration {i} exceeded "
                      f"{args.timeout}s; VP alive={alive}", file=sys.stderr)
                raise AssertionError(f"HANG after {done} resets (iteration {i})")

            done = i
            if i % 10 == 0 or i == 1:
                print(f"  reset {i}/{args.resets}: counter={counter}", flush=True)

        print(f"reset-stress: {done}/{args.resets} resets, no hang")

    finally:
        pool.shutdown(wait=False)
        if mcp:
            try:
                mcp.close()
            except Exception:
                pass
        vp.send_signal(signal.SIGTERM)
        try:
            out, _ = vp.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            vp.kill()
            out, _ = vp.communicate()
        if out:
            print("--- VP stdout (tail) ---")
            print(out[-3000:])


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("-e", "--exe", required=True, metavar="VP")
    p.add_argument("-l", "--lua", required=True, metavar="LUA")
    p.add_argument("-m", "--mcp", required=True, metavar="MCP")
    p.add_argument("-f", "--fw", default="", metavar="FW")
    p.add_argument("-n", "--resets", type=int, default=200)
    p.add_argument("--cores", type=int, default=1)
    p.add_argument("--timeout", type=float, default=30.0)
    args = p.parse_args()

    required = [args.exe, args.lua, args.mcp] + ([args.fw] if args.fw else [])
    for path in required:
        if not os.path.isfile(path):
            print(f"ERROR: file not found: {path}", file=sys.stderr)
            return 1

    run_test(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
