/*
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include <mcd_debug.h>

namespace {

// Minimal JSON value, parser and emitter: only the shapes an MCP client sends
// are supported, this is not a general library.

struct JsonValue {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;

    const JsonValue* find(const std::string& k) const
    {
        if (type != Obj) return nullptr;
        for (const auto& p : obj)
            if (p.first == k) return &p.second;
        return nullptr;
    }
};

std::string json_escape(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"':
            o += "\\\"";
            break;
        case '\\':
            o += "\\\\";
            break;
        case '\n':
            o += "\\n";
            break;
        case '\r':
            o += "\\r";
            break;
        case '\t':
            o += "\\t";
            break;
        case '\b':
            o += "\\b";
            break;
        case '\f':
            o += "\\f";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c & 0xff);
                o += buf;
            } else {
                o += c;
            }
        }
    }
    return o;
}

std::string json_emit(const JsonValue& v)
{
    switch (v.type) {
    case JsonValue::Null:
        return "null";
    case JsonValue::Bool:
        return v.b ? "true" : "false";
    case JsonValue::Str:
        return "\"" + json_escape(v.str) + "\"";
    case JsonValue::Num: {
        double d = v.num;
        if (d == static_cast<double>(static_cast<int64_t>(d))) return std::to_string(static_cast<int64_t>(d));
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g", d);
        return buf;
    }
    case JsonValue::Arr: {
        std::string o = "[";
        for (size_t i = 0; i < v.arr.size(); ++i) {
            if (i) o += ",";
            o += json_emit(v.arr[i]);
        }
        return o + "]";
    }
    case JsonValue::Obj: {
        std::string o = "{";
        for (size_t i = 0; i < v.obj.size(); ++i) {
            if (i) o += ",";
            o += "\"" + json_escape(v.obj[i].first) + "\":" + json_emit(v.obj[i].second);
        }
        return o + "}";
    }
    }
    return "null";
}

struct Parser {
    const std::string& s;
    size_t i = 0;
    explicit Parser(const std::string& in): s(in) {}

    void ws()
    {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    }

    JsonValue parse()
    {
        ws();
        return value();
    }

    JsonValue value()
    {
        ws();
        if (i >= s.size()) throw std::runtime_error("json: unexpected end");
        char c = s[i];
        if (c == '{') return object();
        if (c == '[') return array();
        if (c == '"') {
            JsonValue v;
            v.type = JsonValue::Str;
            v.str = string_();
            return v;
        }
        if (c == 't' || c == 'f') return boolean();
        if (c == 'n') {
            expect_lit("null");
            return JsonValue{};
        }
        return number();
    }

    void expect_lit(const char* lit)
    {
        for (const char* p = lit; *p; ++p, ++i)
            if (i >= s.size() || s[i] != *p) throw std::runtime_error("json: bad literal");
    }

    JsonValue boolean()
    {
        JsonValue v;
        v.type = JsonValue::Bool;
        if (s[i] == 't') {
            expect_lit("true");
            v.b = true;
        } else {
            expect_lit("false");
            v.b = false;
        }
        return v;
    }

    JsonValue number()
    {
        size_t start = i;
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' ||
                                s[i] == '.' || s[i] == 'e' || s[i] == 'E'))
            ++i;
        if (i == start) throw std::runtime_error("json: bad number");
        JsonValue v;
        v.type = JsonValue::Num;
        v.num = std::strtod(s.substr(start, i - start).c_str(), nullptr);
        return v;
    }

    std::string string_()
    {
        ++i;
        std::string out;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return out;
            if (c == '\\') {
                if (i >= s.size()) break;
                char e = s[i++];
                switch (e) {
                case '"':
                    out += '"';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '/':
                    out += '/';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'u':
                    // \uXXXX is unused by this protocol: skip the 4 hex digits.
                    i += 4;
                    out += '?';
                    break;
                default:
                    out += e;
                    break;
                }
            } else {
                out += c;
            }
        }
        throw std::runtime_error("json: unterminated string");
    }

    JsonValue array()
    {
        JsonValue v;
        v.type = JsonValue::Arr;
        ++i;
        ws();
        if (i < s.size() && s[i] == ']') {
            ++i;
            return v;
        }
        while (true) {
            v.arr.push_back(value());
            ws();
            if (i < s.size() && s[i] == ',') {
                ++i;
                continue;
            }
            if (i < s.size() && s[i] == ']') {
                ++i;
                break;
            }
            throw std::runtime_error("json: bad array");
        }
        return v;
    }

    JsonValue object()
    {
        JsonValue v;
        v.type = JsonValue::Obj;
        ++i;
        ws();
        if (i < s.size() && s[i] == '}') {
            ++i;
            return v;
        }
        while (true) {
            ws();
            if (i >= s.size() || s[i] != '"') throw std::runtime_error("json: bad key");
            std::string key = string_();
            ws();
            if (i >= s.size() || s[i] != ':') throw std::runtime_error("json: expected ':'");
            ++i;
            v.obj.emplace_back(std::move(key), value());
            ws();
            if (i < s.size() && s[i] == ',') {
                ++i;
                continue;
            }
            if (i < s.size() && s[i] == '}') {
                ++i;
                break;
            }
            throw std::runtime_error("json: bad object");
        }
        return v;
    }
};

std::string req_str(const JsonValue* args, const char* key)
{
    const JsonValue* v = args ? args->find(key) : nullptr;
    if (!v || v->type != JsonValue::Str) throw std::runtime_error(std::string("missing string arg: ") + key);
    return v->str;
}

int64_t req_int(const JsonValue* args, const char* key)
{
    const JsonValue* v = args ? args->find(key) : nullptr;
    if (!v || v->type != JsonValue::Num) throw std::runtime_error(std::string("missing integer arg: ") + key);
    return static_cast<int64_t>(v->num);
}

int64_t opt_int(const JsonValue* args, const char* key, int64_t def)
{
    const JsonValue* v = args ? args->find(key) : nullptr;
    if (!v || v->type != JsonValue::Num) return def;
    return static_cast<int64_t>(v->num);
}

uint64_t parse_u64(const std::string& s, int base) { return std::stoull(s, nullptr, base); }

std::vector<uint8_t> parse_hex_bytes(const std::string& in)
{
    std::string s = in;
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
    if (s.size() % 2 != 0) throw std::runtime_error("hex data must have an even number of digits");
    std::vector<uint8_t> out;
    out.reserve(s.size() / 2);
    for (size_t j = 0; j < s.size(); j += 2)
        out.push_back(static_cast<uint8_t>(std::stoul(s.substr(j, 2), nullptr, 16)));
    return out;
}

std::unique_ptr<mcd::Connection> g_conn;
// snapshot name -> {first_regno, values}
std::map<std::string, std::pair<uint32_t, std::vector<uint64_t>>> g_snapshots;

mcd::Core core(uint32_t idx = 0)
{
    if (!g_conn) throw std::runtime_error("not connected (call mcd_connect first)");
    return mcd::Core(*g_conn, idx);
}

std::string status_json()
{
    if (!g_conn) return "{\"connected\":false}";
    std::string o = "{\"connected\":true,\"host\":\"" + json_escape(g_conn->host()) +
                    "\",\"port\":" + std::to_string(g_conn->port());
    o += ",\"systems\":[";
    for (size_t k = 0; k < g_conn->systems().size(); ++k) {
        if (k) o += ",";
        o += "\"" + json_escape(g_conn->systems()[k]) + "\"";
    }
    o += "],\"devices\":[";
    for (size_t k = 0; k < g_conn->devices().size(); ++k) {
        if (k) o += ",";
        o += "\"" + json_escape(g_conn->devices()[k]) + "\"";
    }
    o += "],\"cores\":[";
    for (size_t k = 0; k < g_conn->cores().size(); ++k) {
        if (k) o += ",";
        o += "{\"core_id\":" + std::to_string(g_conn->cores()[k].core_id) +
             ",\"device_id\":" + std::to_string(g_conn->cores()[k].device_id) + "}";
    }
    o += "],\"mem_spaces\":[";
    for (size_t k = 0; k < g_conn->mem_spaces().size(); ++k) {
        if (k) o += ",";
        o += "{\"id\":" + std::to_string(g_conn->mem_spaces()[k].id) + ",\"name\":\"" +
             json_escape(g_conn->mem_spaces()[k].name) + "\"}";
    }
    o += "]}";
    return o;
}

std::string hex_dump(uint64_t addr, const std::vector<uint8_t>& buf)
{
    std::string o;
    char line[32];
    for (size_t off = 0; off < buf.size(); off += 16) {
        std::snprintf(line, sizeof(line), "%016llx: ", static_cast<unsigned long long>(addr + off));
        o += line;
        std::string ascii;
        for (size_t j = 0; j < 16; ++j) {
            if (off + j < buf.size()) {
                char hx[4];
                std::snprintf(hx, sizeof(hx), "%02x ", buf[off + j]);
                o += hx;
                char c = static_cast<char>(buf[off + j]);
                ascii += (c >= 0x20 && c < 0x7f) ? c : '.';
            } else {
                o += "   ";
            }
        }
        o += " " + ascii + "\n";
    }
    return o;
}

std::string describe_text()
{
    if (!g_conn) return "not connected";
    std::string o;
    o += "host: " + g_conn->host() + ":" + std::to_string(g_conn->port()) + "\n";
    o += "systems (" + std::to_string(g_conn->systems().size()) + "):\n";
    for (size_t i = 0; i < g_conn->systems().size(); ++i)
        o += "  [" + std::to_string(i) + "] " + g_conn->systems()[i] + "\n";
    o += "devices (" + std::to_string(g_conn->devices().size()) + "):\n";
    for (size_t i = 0; i < g_conn->devices().size(); ++i)
        o += "  [" + std::to_string(i) + "] " + g_conn->devices()[i] + "\n";
    o += "cores (" + std::to_string(g_conn->cores().size()) + "):\n";
    for (size_t i = 0; i < g_conn->cores().size(); ++i) {
        char line[64];
        std::snprintf(line, sizeof(line), "  cpu%zu  core_id=%u  device_id=%u\n", i, g_conn->cores()[i].core_id,
                      g_conn->cores()[i].device_id);
        o += line;
    }
    o += "mem_spaces (" + std::to_string(g_conn->mem_spaces().size()) + "):\n";
    for (size_t i = 0; i < g_conn->mem_spaces().size(); ++i) {
        o += "  id=" + std::to_string(g_conn->mem_spaces()[i].id) + "  name=" + g_conn->mem_spaces()[i].name + "\n";
    }
    return o;
}

std::string regs_dump_text(uint32_t cpu_idx, uint32_t start_regno, uint32_t count)
{
    mcd::Core c = core(cpu_idx);
    std::string o;
    char line[48];
    for (uint32_t r = start_regno; r < start_regno + count; ++r) {
        uint64_t v = c.read_reg(r);
        std::snprintf(line, sizeof(line), "  r%-3u  0x%016llx\n", r, static_cast<unsigned long long>(v));
        o += line;
    }
    return o;
}

mcd::BpType parse_bp_type(const std::string& s)
{
    if (s == "sw" || s == "swbreak" || s == "break" || s == "breakpoint") return mcd::BpType::SwBreak;
    if (s == "hw" || s == "hwbreak") return mcd::BpType::HwBreak;
    if (s == "write" || s == "wwatch" || s == "watch") return mcd::BpType::WatchWrite;
    if (s == "read" || s == "rwatch") return mcd::BpType::WatchRead;
    if (s == "access" || s == "awatch" || s == "rw") return mcd::BpType::WatchAccess;
    throw std::runtime_error("unknown breakpoint type '" + s + "' (use sw|hw|write|read|access)");
}

const char* bp_type_name(mcd::BpType t)
{
    switch (t) {
    case mcd::BpType::SwBreak:
        return "sw";
    case mcd::BpType::HwBreak:
        return "hw";
    case mcd::BpType::WatchWrite:
        return "write-watch";
    case mcd::BpType::WatchRead:
        return "read-watch";
    case mcd::BpType::WatchAccess:
        return "access-watch";
    }
    return "?";
}

const char* stop_reason_name(uint32_t reason)
{
    switch (reason) {
    case 0:
        return "running";
    case 1:
        return "halted";
    case 2:
        return "breakpoint";
    case 3:
        return "write-watchpoint";
    case 4:
        return "read-watchpoint";
    case 5:
        return "access-watchpoint";
    case 6:
        return "signal";
    }
    return "unknown";
}

// Returns the text body of the tool result; throws on error.
std::string call_tool(const std::string& name, const JsonValue* args)
{
    if (name == "mcd_connect") {
        std::string host = req_str(args, "host");
        uint16_t port = static_cast<uint16_t>(opt_int(args, "port", 1235));
        g_conn = std::make_unique<mcd::Connection>(host, port);
        return "connected to " + host + ":" + std::to_string(port) + ", " + std::to_string(g_conn->cores().size()) +
               " cores, " + std::to_string(g_conn->mem_spaces().size()) + " mem_spaces";
    }
    if (name == "mcd_disconnect") {
        g_conn.reset();
        return "disconnected";
    }
    if (name == "mcd_status") {
        return status_json();
    }
    if (name == "mcd_run") {
        core().run();
        return "ok";
    }
    if (name == "mcd_stop") {
        core().stop();
        return "ok";
    }
    if (name == "mcd_step") {
        core(static_cast<uint32_t>(opt_int(args, "cpu_idx", 0))).step();
        return "ok";
    }
    if (name == "mcd_read_reg") {
        uint32_t cpu_idx = static_cast<uint32_t>(opt_int(args, "cpu_idx", 0));
        uint64_t v = core(cpu_idx).read_reg(static_cast<uint32_t>(req_int(args, "regno")));
        char buf[32];
        std::snprintf(buf, sizeof(buf), "0x%016llx", static_cast<unsigned long long>(v));
        return buf;
    }
    if (name == "mcd_write_reg") {
        uint32_t cpu_idx = static_cast<uint32_t>(opt_int(args, "cpu_idx", 0));
        uint32_t regno = static_cast<uint32_t>(req_int(args, "regno"));
        uint64_t val = parse_u64(req_str(args, "value"), 0); // auto-detect hex/dec
        core(cpu_idx).write_reg(regno, val);
        return "ok";
    }
    if (name == "mcd_read_mem") {
        uint64_t addr = parse_u64(req_str(args, "addr"), 16);
        uint32_t len = static_cast<uint32_t>(req_int(args, "len"));
        uint32_t space = static_cast<uint32_t>(opt_int(args, "space_id", 0));
        return hex_dump(addr, core().read_mem(addr, len, space));
    }
    if (name == "mcd_write_mem") {
        uint64_t addr = parse_u64(req_str(args, "addr"), 16);
        std::vector<uint8_t> data = parse_hex_bytes(req_str(args, "data"));
        uint32_t space = static_cast<uint32_t>(opt_int(args, "space_id", 0));
        core().write_mem(addr, data, space);
        return "ok";
    }
    if (name == "mcd_refresh") {
        if (!g_conn) throw std::runtime_error("not connected (call mcd_connect first)");
        g_conn->refresh();
        return status_json();
    }
    if (name == "mcd_describe") {
        return describe_text();
    }
    if (name == "mcd_regs_dump") {
        uint32_t cpu_idx = static_cast<uint32_t>(opt_int(args, "cpu_idx", 0));
        uint32_t start = static_cast<uint32_t>(opt_int(args, "start_regno", 0));
        uint32_t count = static_cast<uint32_t>(opt_int(args, "count", 32));
        return "registers cpu" + std::to_string(cpu_idx) + " r" + std::to_string(start) + ".." +
               std::to_string(start + count - 1) + ":\n" + regs_dump_text(cpu_idx, start, count);
    }
    if (name == "mcd_snapshot") {
        std::string snap_name = req_str(args, "name");
        uint32_t cpu_idx = static_cast<uint32_t>(opt_int(args, "cpu_idx", 0));
        uint32_t start = static_cast<uint32_t>(opt_int(args, "start_regno", 0));
        uint32_t count = static_cast<uint32_t>(opt_int(args, "count", 32));
        mcd::Core c = core(cpu_idx);
        std::vector<uint64_t> vals;
        vals.reserve(count);
        for (uint32_t r = start; r < start + count; ++r) vals.push_back(c.read_reg(r));
        g_snapshots[snap_name] = { start, std::move(vals) };
        return "snapshot '" + snap_name + "' saved: " + std::to_string(count) + " regs from r" + std::to_string(start);
    }
    if (name == "mcd_diff") {
        std::string snap_name = req_str(args, "name");
        auto it = g_snapshots.find(snap_name);
        if (it == g_snapshots.end()) throw std::runtime_error("no snapshot named '" + snap_name + "'");
        uint32_t cpu_idx = static_cast<uint32_t>(opt_int(args, "cpu_idx", 0));
        uint32_t start = it->second.first;
        const std::vector<uint64_t>& saved = it->second.second;
        mcd::Core c = core(cpu_idx);
        std::string o;
        bool any = false;
        char line[80];
        for (uint32_t i = 0; i < static_cast<uint32_t>(saved.size()); ++i) {
            uint64_t now = c.read_reg(start + i);
            if (now != saved[i]) {
                std::snprintf(line, sizeof(line), "  r%-3u  was=0x%016llx  now=0x%016llx\n", start + i,
                              static_cast<unsigned long long>(saved[i]), static_cast<unsigned long long>(now));
                o += line;
                any = true;
            }
        }
        return any ? ("changed registers (cpu" + std::to_string(cpu_idx) + " vs '" + snap_name + "'):\n" + o)
                   : "no changes (cpu" + std::to_string(cpu_idx) + " vs '" + snap_name + "')";
    }
    if (name == "mcd_set_bp") {
        uint32_t cpu_idx = static_cast<uint32_t>(opt_int(args, "cpu_idx", 0));
        uint64_t addr = parse_u64(req_str(args, "addr"), 16);
        mcd::BpType type = parse_bp_type(args && args->find("type") ? req_str(args, "type") : "sw");
        uint32_t kind = static_cast<uint32_t>(opt_int(args, "len", 0));
        core(cpu_idx).set_breakpoint(addr, type, kind);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "set %s @0x%llx on cpu%u", bp_type_name(type),
                      static_cast<unsigned long long>(addr), cpu_idx);
        return buf;
    }
    if (name == "mcd_clear_bp") {
        uint32_t cpu_idx = static_cast<uint32_t>(opt_int(args, "cpu_idx", 0));
        uint64_t addr = parse_u64(req_str(args, "addr"), 16);
        mcd::BpType type = parse_bp_type(args && args->find("type") ? req_str(args, "type") : "sw");
        uint32_t kind = static_cast<uint32_t>(opt_int(args, "len", 0));
        core(cpu_idx).clear_breakpoint(addr, type, kind);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "cleared %s @0x%llx on cpu%u", bp_type_name(type),
                      static_cast<unsigned long long>(addr), cpu_idx);
        return buf;
    }
    if (name == "mcd_list_bp") {
        if (!g_conn) throw std::runtime_error("not connected (call mcd_connect first)");
        auto bps = g_conn->list_breakpoints();
        if (bps.empty()) return "no breakpoints or watchpoints set";
        std::string o = "breakpoints/watchpoints (" + std::to_string(bps.size()) + "):\n";
        char line[96];
        for (const auto& bp : bps) {
            std::snprintf(line, sizeof(line), "  cpu%u  %-13s @0x%llx  len=%u\n", bp.cpu, bp_type_name(bp.type),
                          static_cast<unsigned long long>(bp.addr), bp.kind);
            o += line;
        }
        return o;
    }
    if (name == "mcd_wait_stop") {
        uint32_t cpu_idx = static_cast<uint32_t>(opt_int(args, "cpu_idx", 0));
        uint32_t timeout_ms = static_cast<uint32_t>(opt_int(args, "timeout_ms", 2000));
        mcd::Core c = core(cpu_idx);
        mcd::StopEvent ev = c.wait_stop(timeout_ms);
        if (!ev.stopped) {
            return "still running (no stop within " + std::to_string(timeout_ms) + " ms)";
        }
        /* Halted: PC is AArch64 GDB reg 32. */
        std::string o = std::string("stopped: ") + stop_reason_name(ev.reason);
        char buf[64];
        try {
            uint64_t pc = c.read_reg(32);
            std::snprintf(buf, sizeof(buf), " pc=0x%llx", static_cast<unsigned long long>(pc));
            o += buf;
        } catch (const std::exception&) {
            /* PC read is best-effort. */
        }
        if (ev.reason >= 3 && ev.reason <= 5) {
            std::snprintf(buf, sizeof(buf), " watch_addr=0x%llx", static_cast<unsigned long long>(ev.watch_addr));
            o += buf;
        }
        return o;
    }
    throw std::runtime_error("unknown tool: " + name);
}

const char* k_tools_list =
    "[{\"name\":\"mcd_connect\",\"description\":\"Connect to an mcd_server instance.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"host\":{\"type\":\"string\"},"
    "\"port\":{\"type\":\"integer\"}},\"required\":[\"host\"]}},"
    "{\"name\":\"mcd_disconnect\",\"description\":\"Close the active connection.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
    "{\"name\":\"mcd_status\",\"description\":\"JSON summary of connection, systems, devices, cores, mem_spaces.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
    "{\"name\":\"mcd_run\",\"description\":\"Resume the target.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
    "{\"name\":\"mcd_stop\",\"description\":\"Halt the target.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
    "{\"name\":\"mcd_step\",\"description\":\"Single-step one core.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"cpu_idx\":{\"type\":\"integer\"}}}},"
    "{\"name\":\"mcd_read_reg\",\"description\":\"Read a GDB register by number for a core (default cpu0); returns "
    "0x-prefixed hex. Register numbering follows the target's GDB layout (AArch64: x0..x30=0..30, sp=31, pc=32).\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"cpu_idx\":{\"type\":\"integer\"},\"regno\":{\"type\":"
    "\"integer\"}},"
    "\"required\":[\"regno\"]}},"
    "{\"name\":\"mcd_write_reg\",\"description\":\"Write a GDB register for a core (default cpu0); value is hex "
    "(0x...) or decimal.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"cpu_idx\":{\"type\":\"integer\"},\"regno\":{\"type\":"
    "\"integer\"},"
    "\"value\":{\"type\":\"string\"}},\"required\":[\"regno\",\"value\"]}},"
    "{\"name\":\"mcd_read_mem\",\"description\":\"Read memory; returns a hex dump.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"addr\":{\"type\":\"string\"},"
    "\"len\":{\"type\":\"integer\"},\"space_id\":{\"type\":\"integer\"}},"
    "\"required\":[\"addr\",\"len\"]}},"
    "{\"name\":\"mcd_write_mem\",\"description\":\"Write memory from a hex byte string.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"addr\":{\"type\":\"string\"},"
    "\"data\":{\"type\":\"string\"},\"space_id\":{\"type\":\"integer\"}},"
    "\"required\":[\"addr\",\"data\"]}},"
    "{\"name\":\"mcd_refresh\",\"description\":\"Re-query the target and return the updated status.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
    "{\"name\":\"mcd_describe\",\"description\":\"Human-readable topology table: host, systems, devices, cores, "
    "mem_spaces.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
    "{\"name\":\"mcd_regs_dump\",\"description\":\"Dump a range of GDB registers for a core (default: r0..r31 of "
    "cpu0).\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"cpu_idx\":{\"type\":\"integer\"},"
    "\"start_regno\":{\"type\":\"integer\"},\"count\":{\"type\":\"integer\"}}}},"
    "{\"name\":\"mcd_snapshot\",\"description\":\"Save a named register snapshot for later diff.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},"
    "\"cpu_idx\":{\"type\":\"integer\"},\"start_regno\":{\"type\":\"integer\"},"
    "\"count\":{\"type\":\"integer\"}},\"required\":[\"name\"]}},"
    "{\"name\":\"mcd_diff\",\"description\":\"Show which registers changed since the named snapshot.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\"},"
    "\"cpu_idx\":{\"type\":\"integer\"}},\"required\":[\"name\"]}},"
    "{\"name\":\"mcd_set_bp\",\"description\":\"Set a breakpoint or watchpoint at a hex address. type: "
    "sw|hw|write|read|access (default sw). len is the watch length in bytes (default 4). Combine with mcd_run + "
    "mcd_wait_stop to run to it.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"addr\":{\"type\":\"string\"},"
    "\"type\":{\"type\":\"string\"},\"len\":{\"type\":\"integer\"},\"cpu_idx\":{\"type\":\"integer\"}},"
    "\"required\":[\"addr\"]}},"
    "{\"name\":\"mcd_clear_bp\",\"description\":\"Remove a breakpoint or watchpoint previously set at a hex address "
    "(same type as when set).\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"addr\":{\"type\":\"string\"},"
    "\"type\":{\"type\":\"string\"},\"len\":{\"type\":\"integer\"},\"cpu_idx\":{\"type\":\"integer\"}},"
    "\"required\":[\"addr\"]}},"
    "{\"name\":\"mcd_list_bp\",\"description\":\"List the breakpoints and watchpoints currently installed on the "
    "target.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
    "{\"name\":\"mcd_wait_stop\",\"description\":\"Wait for a core to stop (breakpoint/watchpoint/signal). Returns the "
    "stop reason and PC once halted, or reports it is still running if timeout_ms (default 2000) elapses first.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"cpu_idx\":{\"type\":\"integer\"},"
    "\"timeout_ms\":{\"type\":\"integer\"}}}}]";

void send(const std::string& msg)
{
    std::cout << msg << "\n";
    std::cout.flush();
}

std::string reply_result(const std::string& id_raw, const std::string& result_json)
{
    return "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"result\":" + result_json + "}";
}

std::string reply_error(const std::string& id_raw, int code, const std::string& message)
{
    return "{\"jsonrpc\":\"2.0\",\"id\":" + id_raw + ",\"error\":{\"code\":" + std::to_string(code) +
           ",\"message\":\"" + json_escape(message) + "\"}}";
}

std::string tool_content(const std::string& text, bool is_error)
{
    return "{\"content\":[{\"type\":\"text\",\"text\":\"" + json_escape(text) +
           "\"}],\"isError\":" + (is_error ? "true" : "false") + "}";
}

void dispatch(const JsonValue& req)
{
    const JsonValue* method_v = req.find("method");
    const JsonValue* id_v = req.find("id");
    std::string method = (method_v && method_v->type == JsonValue::Str) ? method_v->str : "";

    // JSON-RPC: a request without an id is a notification and must get no reply.
    bool is_notification = (id_v == nullptr);
    std::string id_raw = id_v ? json_emit(*id_v) : "null";

    if (method == "initialize") {
        send(reply_result(id_raw,
                          "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},"
                          "\"serverInfo\":{\"name\":\"mcd-mcp\",\"version\":\"0.1\"}}"));
        return;
    }
    if (method == "notifications/initialized") {
        return;
    }
    if (method == "tools/list") {
        send(reply_result(id_raw, std::string("{\"tools\":") + k_tools_list + "}"));
        return;
    }
    if (method == "tools/call") {
        const JsonValue* params = req.find("params");
        const JsonValue* name_v = params ? params->find("name") : nullptr;
        const JsonValue* args = params ? params->find("arguments") : nullptr;
        std::string name = (name_v && name_v->type == JsonValue::Str) ? name_v->str : "";
        try {
            std::string text = call_tool(name, args);
            send(reply_result(id_raw, tool_content(text, false)));
        } catch (const std::exception& e) {
            send(reply_result(id_raw, tool_content(e.what(), true)));
        }
        return;
    }

    if (is_notification) return;
    send(reply_error(id_raw, -32601, "Method not found"));
}

} // namespace

int main()
{
    std::ios::sync_with_stdio(false);
#ifdef _WIN32
    /* MCP frames are newline-delimited JSON: text mode would rewrite the
     * delimiters in both directions. */
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back(); /* CRLF-terminated host */
        if (line.empty()) continue;
        try {
            Parser p(line);
            JsonValue req = p.parse();
            dispatch(req);
        } catch (const std::exception& e) {
            send(reply_error("null", -32700, std::string("Parse error: ") + e.what()));
        }
    }
    return 0;
}
