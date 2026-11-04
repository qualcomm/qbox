
-- ** Convenience functions **
function file_exists(name)
    local f = io.open(name, "r")
    if f ~= nil then
        io.close(f)
        return true
    else
        return false
    end
end

function valid_file(file)
    local f = io.open(file, "r")
    if (f == nil) then
        print("ERROR:")
        print(file .. " Not found.")
        os.exit(1)
    end
    io.close(f);
    return file
end


function get_system()
    local handle = io.popen("uname -s")
    local result = handle:read("*a")
    handle:close()

    result = result:gsub("%s+", "") -- trim whitespace

    if result == "Darwin" then
        return "macOS"
    elseif result == "Linux" then
        return "Linux"
    elseif result:match("^MINGW") then
        return "Windows"
    else
        return result -- return the actual system name if not recognized
    end
end

function on_macOS()
    return (get_system() == "macOS")
end

function on_Windows()
    return (get_system() == "Windows")
end

function tableMerge(t1, t2)
    for k, v in pairs(t2) do
        if type(v) == "table" then
            if type(t1[k] or false) == "table" then
                tableMerge(t1[k] or {}, t2[k] or {})
            else
                t1[k] = v
            end
        else
            t1[k] = v
        end
    end
    return t1
end
function tableJoin(t1, t2)
    for i = 1, #t2 do
        t1[#t1 + 1] = t2[i]
    end
    return t1
end

-- Override assert() as the default leads to odd errors in qqvp
function assert(exp, msg)
    if msg == nil then
        msg = "Assertion failed!"
    end
    if not exp then
        print(msg)
        print(debug.traceback())
        os.exit(1)
    end
    return exp
end

function dofile_with_args(filename, ...)
    local f = assert(loadfile(filename))
    return f(...)
end

function sleep(n)
    local t0 = os.clock();
    while os.clock() - t0 <= n do
    end
end

function set_hexagon_property(prop_name, value)
    local cid = 0
    while true do
        local cluster = platform.hex_plugin.config["hexagon_cluster_" .. cid]
        if cluster == nil then
            break
        end
        local tid = 0
        while true do
            local thread = cluster["hexagon_thread_" .. tid]
            if thread == nil then
                break
            end
            thread[prop_name] = value
            tid = tid + 1
        end
        cid = cid + 1
    end
end

function replace_str(str_name, pattern, sub_name)
    return str_name:gsub(pattern, sub_name)
end


function disable_all_DSPs()
    del(platform, "hex_plugin")
    del(platform, "apss_hm_apss_shared_apss_intu")
    del_many(platform, "tbu_", 0)
    del_hex_irqs("ipcc_0")
    del_hex_irqs("ipc_router_top")
end


-- ** End of convenience functions **
