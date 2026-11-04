-- Platform for the reset-stress test: a real AArch64 board (router, RAM, uart,
-- loader) with an mcd_server to drive resets and a reset_gpio bridging the
-- SystemC and QEMU reset domains. The firmware spins forever bumping a counter,
-- so the target stays live and a restart is observable.
--
-- Presets must precede the lua file on the command line; string values are
-- JSON-quoted:
--   reset-stress-vp -p mcd.port=1235 -p 'fw="/path/hello-debug.elf"' -p cores=1 \
--                   --gs_luafile reset-stress.lua
-- Defaults: port 1235, hello-debug.elf beside this file, one core.

function script_path()
    local src = debug.getinfo(2, "S").source:sub(2)
    return src:match("(.*/)")
end
local base = script_path()

local mcd_port = tonumber(GET("mcd.port")) or 1235
local num_cores = tonumber(GET("cores")) or 1

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

    -- Bridges QEMU's system_reset (which MCD's RESET drives) into a SystemC
    -- reset, and back. reset_out must feed reset_in back: the QEMU-side callback
    -- blocks until the SystemC reset it started is acknowledged there.
    reset_gpio_0 = {
        moduletype = "reset_gpio",
        dylib_path = "reset_gpio",
        args       = { "&qemu_inst" },
        reset_out  = { bind = "&reset_gpio_0.reset_in" },
    },

    mcd_server = {
        moduletype = "mcd_server",
        dylib_path = "mcd_server",
        mcd_port   = mcd_port,
    },
}

-- CPUs cpu_0 .. cpu_<n-1> on the one qemu_inst, so a single gdb stub serves all.
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
