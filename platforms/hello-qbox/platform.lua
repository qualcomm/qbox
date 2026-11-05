-- Resolve paths relative to this config file so that hello.elf can
-- be found regardless of the working directory.
function script_path()
    local src = debug.getinfo(2, "S").source:sub(2)
    return src:match("(.*/)")
end
local base = script_path()

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

    cpu_0 = {
        moduletype   = "cpu_arm_cortexA53",
        args         = { "&qemu_inst" },
        mem          = { bind = "&router.target_socket" },
        rvbar        = 0x80000000,
        has_el3      = true,
        has_el2      = true,
        psci_conduit = "hvc",
    },

    charbackend_stdio_0 = {
        moduletype = "char_backend_stdio",
        read_write = true,
    },

    -- PL011 UART at 0x09000000, forwarded to stdout via stdio
    -- backend. No IRQ connection needed for polled TX.
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
        { elf_file = base .. "build/hello.elf" },
    },
}
