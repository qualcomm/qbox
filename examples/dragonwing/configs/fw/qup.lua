local use_socket = GET("use_socket") or false  -- false for stdio, true for socket
local interactive_mode = GET("interactive_mode") or false  -- Used e.g. to switch console highlighting on
local python_dir = GET("python_dir") or ""  -- The directory which contains your python modules.

function char_backend_stdio(args)
    local backend = {
        qup_type = args[1],
        moduletype = "char_backend_stdio",
        read_write = args.rw,
        ansi_highlight = interactive_mode and "\x1B[1m" or "",
        biflow_socket = { bind = "&" .. args[1] .. "_qup_" .. tostring(args[2]) .. ".backend_socket" }
    };
    return backend
end

function char_backend_socket(args)
    local backend
    if (use_socket) then
        backend = {
            qup_type = args[1],
            moduletype = "char_backend_socket",
            address = "127.0.0.1:" .. tostring(args.port),
            server = true,
            nowait = true,
            sigquit = true,
            biflow_socket = { bind = "&" .. args[1] .. "_qup_" .. tostring(args[2]) .. ".backend_socket" }
        };
    else
        print("failed to set char_backend_socket at qup_index ", args[2], " switching to char_backend_stdio. make sure use_socket=true ")
        backend = char_backend_stdio({"uart", args[2], rw = true})
    end
    return backend
end

function loop_back_backend(args)
    local backend = {
            qup_type = args[1],
            moduletype = "loop_back_backend",
            master = args.master,
            biflow_socket = { bind = "&" .. args[1] .. "_qup_" .. tostring(args[2]) .. ".backend_socket" }
        };
        return backend
end

function py_i2c_backend(args)
    local backend
    if (file_exists(python_dir .. "py-i2c.py")) then
        backend = {
            qup_type = args[1],
            moduletype = "python_binder",
            py_module_name = "py-i2c",
            current_mod_id_prefix = "i2c_",
            py_module_dir = python_dir,
            py_module_args = "--address " .. args.slave_addr .. " -d",
            biflow_socket_num = 1,
            initiator_signals_num = 1,
            biflow_socket_0 = { bind = "&" .. args[1] .. "_qup_" .. tostring(args[2]) .. ".backend_socket" }
        };
    else
        print("failed to set py-i2c backend at qup_index ", args[2], " switching to char_backend_stdio. make sure the python_dir path is set correctly")
        backend = char_backend_stdio({"uart", args[2], rw = false})
    end
    return backend
end

function py_uart_backend(args)
    local backend
    if (file_exists(python_dir .. "py-uart.py")) then
        backend = {
            qup_type = args[1],
            moduletype = "python_binder",
            py_module_name = "py-uart",
            current_mod_id_prefix = "uart_",
            py_module_dir = python_dir,
            py_module_args = "--enable_input --baudrate 115200",
            biflow_socket_num = 1,
            biflow_socket_0 = { bind = "&" .. args[1] .. "_qup_" .. tostring(args[2]) .. ".backend_socket" }
        };
    else
        print("failed to set py-uart backend at qup_index ", args[2], " switching to char_backend_stdio. make sure the python_dir path is set correctly")
        backend = char_backend_stdio({"uart", args[2], rw = true})
    end
    return backend
end

function legacy_char_backend_stdio(args)
    local backend = {
            qup_type = args[1],
            moduletype = "legacy_char_backend_stdio",
            args = args.args,
            biflow_socket = { bind = "&" .. args[1] .. "_qup_" .. tostring(args[2]) .. ".backend_socket" }
        };
        return backend
end
