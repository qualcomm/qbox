#!/usr/bin/env python3
# Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Pure-software unit tests for mcd_mcp (no virtual platform): JSON-RPC framing,
# tools/list schema, argument validation, error reporting.
#
# Argument (passed by CTest):
#   -m / --mcp   path to the mcd_mcp binary

import argparse
import json
import os
import subprocess
import sys


class MCP:
    """Drive an mcd_mcp subprocess over JSON-RPC 2.0 on stdio."""

    def __init__(self, mcp_exe: str) -> None:
        self._proc = subprocess.Popen(
            [mcp_exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, bufsize=1)
        self._id = 0

    def rpc(self, method: str, params=None, raw: str = None) -> dict:
        """Send one request (or a raw line) and return the parsed reply."""
        if raw is not None:
            line = raw
        else:
            self._id += 1
            msg = {"jsonrpc": "2.0", "id": self._id, "method": method}
            if params is not None:
                msg["params"] = params
            line = json.dumps(msg)
        self._proc.stdin.write(line + "\n")
        self._proc.stdin.flush()
        return json.loads(self._proc.stdout.readline())

    def notify(self, method: str) -> None:
        self._proc.stdin.write(json.dumps({"jsonrpc": "2.0", "method": method}) + "\n")
        self._proc.stdin.flush()

    def tool(self, name: str, args: dict) -> dict:
        return self.rpc("tools/call", {"name": name, "arguments": args})

    def close(self) -> None:
        try:
            self._proc.stdin.close()
        except Exception:
            pass
        self._proc.terminate()
        self._proc.wait(timeout=5)


CHECKS = 0


def check(cond: bool, msg: str) -> None:
    global CHECKS
    CHECKS += 1
    if not cond:
        raise AssertionError(msg)


def run(mcp_exe: str) -> None:
    m = MCP(mcp_exe)
    try:
        r = m.rpc("initialize", {"protocolVersion": "2024-11-05",
                                 "capabilities": {}, "clientInfo": {"name": "t", "version": "0"}})
        check(r.get("result", {}).get("protocolVersion") == "2024-11-05",
              f"initialize should report protocol version: {r!r}")
        check("serverInfo" in r["result"], "initialize should include serverInfo")
        m.notify("notifications/initialized")

        # tools/list: every advertised tool must have name/description/inputSchema.
        r = m.rpc("tools/list")
        tools = r["result"]["tools"]
        names = {t["name"] for t in tools}
        for expected in ("mcd_connect", "mcd_read_mem", "mcd_write_mem",
                         "mcd_read_reg", "mcd_write_reg", "mcd_run",
                         "mcd_stop", "mcd_step", "mcd_status", "mcd_describe",
                         "mcd_set_bp", "mcd_clear_bp", "mcd_list_bp",
                         "mcd_wait_stop"):
            check(expected in names, f"tools/list missing {expected!r}")
        for t in tools:
            check("description" in t and t["description"], f"{t['name']} lacks description")
            check(t.get("inputSchema", {}).get("type") == "object",
                  f"{t['name']} inputSchema should be an object")

        r = m.tool("mcd_status", {})
        body = r["result"]["content"][0]["text"]
        check(json.loads(body) == {"connected": False},
              f"status before connect should be disconnected: {body!r}")
        check(r["result"]["isError"] is False, "status is not an error")

        # Tools needing a connection fail cleanly (isError) rather than crashing.
        for name, args in (("mcd_read_reg", {"regno": 0}),
                           ("mcd_read_mem", {"addr": "1000", "len": 4}),
                           ("mcd_step", {}),
                           ("mcd_run", {}),
                           ("mcd_set_bp", {"addr": "80000000"}),
                           ("mcd_clear_bp", {"addr": "80000000"}),
                           ("mcd_list_bp", {}),
                           ("mcd_wait_stop", {})):
            r = m.tool(name, args)
            check(r["result"]["isError"] is True,
                  f"{name} before connect should be an error")
            check("not connected" in r["result"]["content"][0]["text"],
                  f"{name} should explain it is not connected: {r!r}")

        # Missing required argument is reported, not crashed.
        r = m.tool("mcd_read_mem", {"addr": "1000"})  # no len
        check(r["result"]["isError"] is True, "missing len should error")
        check("len" in r["result"]["content"][0]["text"], "error should name the missing arg")

        r = m.tool("mcd_set_bp", {})  # no addr
        check(r["result"]["isError"] is True, "missing addr should error")
        check("addr" in r["result"]["content"][0]["text"], "error should name the missing arg")

        # Bad breakpoint type is rejected before the connection is checked.
        r = m.tool("mcd_set_bp", {"addr": "80000000", "type": "bogus"})
        check(r["result"]["isError"] is True, "bad bp type should error")
        check("breakpoint type" in r["result"]["content"][0]["text"],
              f"error should explain the bad bp type: {r!r}")

        r = m.tool("nope", {})
        check(r["result"]["isError"] is True, "unknown tool should error")
        check("unknown tool" in r["result"]["content"][0]["text"], "should say unknown tool")

        # Unknown method -> JSON-RPC error object (not a tool result).
        r = m.rpc("bogus/method")
        check(r.get("error", {}).get("code") == -32601, f"unknown method -> -32601: {r!r}")

        # Malformed JSON line -> parse error, id null.
        r = m.rpc(None, raw="this is not json")
        check(r.get("error", {}).get("code") == -32700, f"bad json -> -32700: {r!r}")
        check(r.get("id") is None, "parse error id should be null")

        r = m.tool("mcd_status", {})
        check(json.loads(r["result"]["content"][0]["text"]) == {"connected": False},
              "server should still respond after a parse error")

        print(f"ALL {CHECKS} UNIT CHECKS PASSED")
    finally:
        m.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-m", "--mcp", required=True, help="Path to mcd_mcp binary")
    args = ap.parse_args()
    if not os.path.isfile(args.mcp):
        print(f"ERROR: not found: {args.mcp}", file=sys.stderr)
        return 1
    run(args.mcp)
    return 0


if __name__ == "__main__":
    sys.exit(main())
