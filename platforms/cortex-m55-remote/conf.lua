-- Virtual platform configuration
-- Commented out parameters show default values

function top()
    local str = debug.getinfo(2, "S").source:sub(2)
    if str:match("(.*/)")
    then
        return str:match("(.*/)")
    else 
        return "./"
    end
 end

EXECUTABLE_PATH = GET("executable_path")

if EXECUTABLE_PATH == nil then
    print("Error: executable_path is not set")
end

function formatExecutable(executable)
    local isWindows = package.config:sub(1,1) == '\\'

    if isWindows then
        return "\\" .. executable .. ".exe"
    else
        return "/" .. executable
    end
end


platform = {
    moduletype = "ContainerDeferModulesConstruct";
    quantum_ns = 10000000;

    router = {
        moduletype="router";
        log_level=0;
    },

    ram_0 = {
            moduletype = "gs_memory",
            target_socket = {address=0x0, size=0x20000, bind = "&router.initiator_socket"},
            shared_memory=true,
            load={bin_file=top().."fw/cortex-m55/cortex-m55.bin", offset=0},
        },

    keep_alive_0 = {
        moduletype = "keep_alive";
    },

    charbackend_stdio_0 = {
       moduletype = "char_backend_stdio";
       read_write = true;
       ansi_highlight = "";
    };

    pl011_uart_0 =  {
        moduletype = "Pl011",
        dylib_path = "uart-pl011";
        target_socket = {address= 0xc0000000, size=0x1000, bind = "&router.initiator_socket"},
        irq = {bind = "&plugin_0.target_signal_socket_0"},
        backend_socket = { bind = "&charbackend_stdio_0.biflow_socket"  },
    },
    
    irq_generator = {
        target_socket = {address= 0xc0001000, size=0x1000, bind = "&router.initiator_socket"},
        irq = {bind="&plugin_0.target_signal_socket_1"},
        nmi = {bind="&plugin_0.target_signal_socket_2"},
        S_SysTick = {bind = "&plugin_0.target_signal_socket_3"},
    },
        
    plugin_0 = {
        moduletype = "RemotePass", -- can be replaced by 'Container'
        exec_path = EXECUTABLE_PATH..formatExecutable("remote_cpu"),
        remote_argv = {"--param", "log_level=1"},
        tlm_initiator_ports_num = 2,
        tlm_target_ports_num = 0,
        target_signals_num = 4,
        initiator_signals_num = 0,
        initiator_socket_0 = {bind = "&router.target_socket"},
        initiator_socket_1 = {bind = "&router.target_socket"},

        plugin_pass = {
            moduletype = "RemotePass", -- -- can be replaced by 'LocalPass'
            tlm_initiator_ports_num = 0,
            tlm_target_ports_num = 2,
            target_signals_num = 0,
            initiator_signals_num = 4,
            target_socket_0 = {address = 0x0, size = 0xE000E000, bind = "&cpu_0.router.initiator_socket"},
            target_socket_1 = {address = 0xE000E000 + 0x10000 , size = 0x100000, bind = "&cpu_0.router.initiator_socket"},
            initiator_signal_socket_0 = {bind = "&cpu_0.nvic.irq_in_0"},
            initiator_signal_socket_1 = {bind = "&cpu_0.nvic.irq_in_17"},
            initiator_signal_socket_2 = {bind = "&cpu_0.nvic.nmi"},
            initiator_signal_socket_3 = {bind = "&cpu_0.nvic.S_SysTick"},
        },

        qemu_inst_mgr = {
            moduletype = "QemuInstanceManager",
        },

        qemu_inst = {
            moduletype = "QemuInstance",
            args = {"&qemu_inst_mgr", "AARCH64"},
            sync_policy = "multithread-freerunning",
        },

        cpu_0={
            moduletype = "RemoteCPU",
            args = {"&qemu_inst"},
            nvic = { mem = { address = 0xE000E000, size = 0x10000}, num_irq = 64 },
        },
    },
    
}
