-- Platform for the MCD tests: an AArch64 board with an mcd_server, running a
-- firmware that spins forever so the target stays live for a debugger. All cores
-- share one QemuInstance, whose gdb stub presents each vCPU as a gdb thread, so
-- a single gdb_port serves every core.
--
-- Port, firmware and core count are CCI presets, which must precede the lua file
-- on the command line; string values are JSON-quoted:
--   mcd-vp -p mcd.port=1235 \
--          -p 'fw="/path/to/hello-debug.elf"' \
--          -p cores=2 \
--          --gs_luafile mcd-platform.lua
-- Defaults: port 1235, hello-debug.elf beside this file, one core.

function script_path()
    local src = debug.getinfo(2, "S").source:sub(2)
    return src:match("(.*/)")
end
local base = script_path()

local mcd_port = tonumber(GET("mcd.port")) or 1235
local num_cores = tonumber(GET("cores")) or 1

-- fw wins; ctest passes the cross-compiled firmware from the build tree.
local fw = GET("fw")
if not fw or fw == "" then fw = base .. "hello-debug.elf" end

platform = {
    moduletype = "Container",
    quantum_ns = 10000000,

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
        initiator_socket = { bind = "&router.target_socket" },
        { elf_file = fw },
    },

    mcd_server = {
        moduletype = "mcd_server",
        dylib_path = "mcd_server",
        mcd_port   = mcd_port,
    },

    -- No keep_alive component: mcd_server holds the simulation open only while
    -- the debugger is the reason the CPUs are stopped, and releases it on resume.
}

-- CPUs cpu_0 .. cpu_<n-1>, added after the table literal so the count can come
-- from the cores preset.
for i = 0, num_cores - 1 do
    platform["cpu_" .. i] = {
        moduletype   = "cpu_arm_cortexA53",
        args         = { "&qemu_inst" },
        mem          = { bind = "&router.target_socket" },
        rvbar        = 0x80000000,
        has_el3      = true,
        has_el2      = true,
        psci_conduit = "hvc",
    }
end
