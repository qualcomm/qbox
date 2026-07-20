/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "luafile_tool.h"

#include <filesystem>

int lua_cci_get_val(lua_State* L)
{
    if (!lua_checkstack(L, 1)) {
        std::cerr << "Can't allocate stack for lua_cci_get_val() " << std::endl;
    }
    cci::cci_originator orig{ "global" };
    auto broker = cci::cci_get_global_broker(orig);
    /**
     * if the passed argument is not string, luaL_checkstring()
     * will generate a bad argument message.
     */
    const char* cci_param_full_name = luaL_checkstring(L, 1);
    if (!cci_param_full_name) {
        std::cerr << "Can't convert CCI parameter name to string";
        abort();
    }
    cci::cci_value cci_val;
    auto h = broker.get_param_handle(std::string(cci_param_full_name));
    if (h.is_valid()) {
        cci_val = h.get_cci_value();
    } else {
        cci_val = broker.get_preset_cci_value(cci_param_full_name);
    }

    if (cci_val.is_bool()) {
        lua_pushboolean(L, static_cast<int>(cci_val.get_bool()));
    } else if (cci_val.is_int()) {
        lua_pushinteger(L, static_cast<lua_Integer>(cci_val.get_int()));
    } else if (cci_val.is_int64()) {
        lua_pushinteger(L, static_cast<lua_Integer>(cci_val.get_int64()));
    } else if (cci_val.is_uint()) {
        lua_pushinteger(L, static_cast<lua_Integer>(cci_val.get_uint()));
    } else if (cci_val.is_uint64()) {
        lua_pushinteger(L, static_cast<lua_Integer>(cci_val.get_uint64()));
    } else if (cci_val.is_double()) {
        lua_pushnumber(L, static_cast<lua_Number>(cci_val.get_double()));
    } else if (cci_val.is_string()) {
        lua_pushstring(L, cci_val.get_string().c_str());
    } else if (cci_val.is_null()) {
        lua_pushnil(L);
    } else {
        std::cerr << "type of CCI parameter: " << cci_param_full_name << " is not supported!" << std::endl;
        abort();
    }
    return 1; // number of results
}

std::string gs::LuaFile_Tool::rel(std::string& n) const
{
    if (m_orig_name.empty())
        return n;
    else
        return (m_orig_name + n);
}

gs::LuaFile_Tool::LuaFile_Tool(std::string _orig_name)
{
    SCP_DEBUG(()) << "LuaFile_Tool Constructor";
    if (_orig_name.empty())
        m_orig_name = "";
    else
        m_orig_name = _orig_name + ".";
}

int gs::LuaFile_Tool::config(cci::cci_broker_handle a_broker, const char* a_config_file, char* a_images_dir)
{
    const char* config_file_path = a_config_file;
    std::string config_file;
    try {
        config_file = std::filesystem::absolute(a_config_file).generic_string();
        config_file_path = config_file.c_str();
    } catch (const std::filesystem::filesystem_error& e) {
        SCP_WARN(()) << "Could not normalize lua file path '" << a_config_file << "': " << e.what();
    }

    SCP_INFO(()) << "Read lua file '" << config_file_path << "'";

    // start Lua
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    // load a script as the function "config_chunk"
    int error = luaL_loadfile(L, config_file_path);
    switch (error) {
    case 0:
        break;
    case LUA_ERRSYNTAX:
        SCP_ERR(()) << "Syntax error reading config file: " << config_file_path;
        return 1;
    case LUA_ERRMEM:
        SCP_ERR(()) << "Error allocating memory to read config file: " << config_file_path;
        return 1;
    case LUA_ERRFILE:
        SCP_ERR(()) << "Error opening/reading the config file: " << config_file_path;
        return 1;
    default:
        SCP_ERR(()) << "Unknown error loading config file: " << config_file_path;
        return 1;
    }
    lua_setglobal(L, "config_chunk");
    lua_pushcfunction(L, lua_cci_get_val);
    lua_setglobal(L, "GET");

    /**
     * To set lua global variables set_g_vars_from_cmdline() can be called here:
     * set_g_vars_from_cmdline(L, a_lua_globalvars);
     */

    // little script to run the file
    const char* config_loader_template =
        "-- put some commands here to run before the user script\n"
        ""
        "function top(n)\n"
        "  n = n or 2\n"
        "  local str = debug.getinfo(n, 'S').source:sub(2)\n"
        "  if str:match('(.*/)') then\n"
        "    return str:match('(.*/)')\n"
        "  else\n"
        "    return './'\n"
        "  end\n"
        "end\n"
        "\n"
        "function image_file(og_fname)\n"
        "  local fname = top(config_chunk) .. og_fname\n"
        "  local ret = io.open(fname, 'r')\n"
        "  if ret == nil and '%s' ~= '' then\n"
        "    fname = '%s/'..og_fname\n"
        "    ret = io.open(fname, 'r')\n"
        "  end\n"
        "  if ret == nil then\n"
        "    print('ERROR: ' .. og_fname .. ' Not found.')\n"
        "    os.exit(1)\n"
        "  end\n"
        "  return fname\n"
        "end\n"
        "\n"
        "config_chunk()";

    const char* images_dir = a_images_dir ? a_images_dir : "";
    int len = std::snprintf(nullptr, 0, config_loader_template, images_dir, images_dir) + 1;
    std::unique_ptr<char[]> config_loader = std::make_unique<char[]>(len);
    std::snprintf(config_loader.get(), len, config_loader_template, images_dir, images_dir);

    // run
    if (luaL_dostring(L, config_loader.get())) {
        SCP_ERR(()) << lua_tostring(L, -1);
        lua_pop(L, 1); /* pop error message from the stack */
    }

    // traverse the environment table setting global variables as parameters
    //      lua_getfield(L, LUA_GLOBALSINDEX, "_G");
    // getglobal should work for both lua 5.1 and 5.2
    lua_getglobal(L, "_G");
    error = setParamsFromLuaTable(a_broker, L, lua_gettop(L));
    if (error < 0) {
        SCP_INFO(()) << "Error loading lua config file: " << config_file_path;
        return error;
    }
    lua_close(L);

    // remove lua builtins
    a_broker.ignore_unconsumed_preset_values([](const std::pair<std::string, cci::cci_value>& iv) -> bool {
        return ((iv.first)[0] == '_' || iv.first == "math.maxinteger") || (iv.first == "math.mininteger") ||
               (iv.first == "utf8.charpattern");
    });
    return 0;
}

int gs::LuaFile_Tool::setParamsFromLuaTable(cci::cci_broker_handle a_broker, lua_State* L, int t, char* level)
{
    /* start up */
    const int MAX_NAME_SIZE = 1000;
    static char static_key[MAX_NAME_SIZE];
    static char* key;
    // static char value[100]; // used only to convert LUA_TNUMBER
    int should_inc_integer_index_count;
    int integer_index_count = 0;
    char* next_level;
    if (level == NULL) {
        key = static_key;
        level = key;
    }

    /* test for overflow (hopefully unlikelly, so test only after it happens,
     * for sanity) */
    if (level - static_key > MAX_NAME_SIZE) {
        static_key[MAX_NAME_SIZE - 1] = 0;
        SCP_INFO(()) << "FATAL Error: parameter name too big (bigger then " << MAX_NAME_SIZE << "): " << static_key;
    }

    /* is it really a table? */
    if (lua_type(L, t) != LUA_TTABLE) {
        SCP_INFO(()) << "Error: argument is not a table";
        return -1;
    }

    /* traverse table */
    lua_pushnil(L); /* first key */

    /* adjust t if relative index */
    if (t < 0) --t;

    while (lua_next(L, t) != 0) {
        /* reset flag */
        should_inc_integer_index_count = false;

        /* set the key */
        switch (lua_type(L, -2)) {
        case LUA_TNUMBER:
            // key must be integer values (ignore floating part)
            // also convert from 1-based to 0-based indexes (decrement 1)
            should_inc_integer_index_count = true;
#ifdef __MINGW32__
            next_level = level + __mingw_sprintf(level, "%lld", (long long)lua_tonumber(L, -2) - 1);
#else
            next_level = level + snprintf(level, MAX_NAME_SIZE - (level - static_key), "%lld",
                                          (long long)lua_tonumber(L, -2) - 1);
#endif
            break;

        case LUA_TSTRING:
            // avoid using stpcpy as it is not defined in MSVS, use strcpy+length
            // instead
            next_level = strcpy(level, lua_tostring(L, -2));
            next_level += strlen(level);
            break;

        default:
            SCP_INFO(()) << "Error loading lua file: invalid key";
            return -1;
        }

        /* set key value in the database */
        switch (lua_type(L, -1)) {
        case LUA_TNUMBER:
            // Avoid setting some Lua specific values as parameters
            if (strcmp(key, "math.huge") == 0 || strcmp(key, "math.pi") == 0 || 0) {
                if (GC_LUA_DEBUG)
                    SCP_INFO(()) << "(" << lua_typename(L, lua_type(L, -1)) << ") " << key
                                 << "   (ignored because it's Lua specific)";
            } else {
                double num = lua_tonumber(L, -1);
                if ((floor(num) == num && num >= 0.0 && num < ldexp(1.0, 64))) {
                    std::string keys = key;
                    a_broker.set_preset_cci_value(rel(keys), cci::cci_value((uint64_t)num));
                } else if (floor(num) == num && num >= -ldexp(1.0, 63) && num < ldexp(1.0, 63)) {
                    std::string keys = key;
                    a_broker.set_preset_cci_value(rel(keys), cci::cci_value((int64_t)num));
                } else {
                    std::string keys = key;
                    a_broker.set_preset_cci_value(rel(keys), cci::cci_value(num));
                }

                if (GC_LUA_VERBOSE) {
                    std::string keys = key;
                    SCP_INFO(()) << "(SET " << lua_typename(L, lua_type(L, -1)) << ") " << rel(keys).c_str() << " = "
                                 << cci::cci_value(num).to_json().c_str();
                }
                if (should_inc_integer_index_count) ++integer_index_count;
            }
            break;

        case LUA_TBOOLEAN: {
            std::string keys = key;
            a_broker.set_preset_cci_value(rel(keys), cci::cci_value((bool)lua_toboolean(L, -1)));
            if (GC_LUA_VERBOSE)
                SCP_INFO(()) << "(SET " << lua_typename(L, lua_type(L, -1)) << ") " << rel(keys).c_str() << " = "
                             << (lua_toboolean(L, -1) ? "true" : "false");
            if (should_inc_integer_index_count) ++integer_index_count;
            break;
        }
        case LUA_TSTRING:
            // Avoid setting some Lua specific values as parameters
            if (strcmp(key, "_VERSION") == 0 || strcmp(key, "package.cpath") == 0 ||
                strcmp(key, "package.config") == 0 || strcmp(key, "package.path") == 0 || 0) {
                if (GC_LUA_DEBUG)
                    SCP_INFO(()) << "(" << lua_typename(L, lua_type(L, -1)) << ") " << key
                                 << "   (ignored because it's Lua specific)";
            } else {
                std::string keys = key;
                a_broker.set_preset_cci_value(rel(keys), cci::cci_value(std::string(lua_tostring(L, -1))));
                if (GC_LUA_VERBOSE)
                    SCP_INFO(()) << "(SET " << lua_typename(L, lua_type(L, -1)) << ") " << rel(keys).c_str() << " = "
                                 << lua_tostring(L, -1);
                if (should_inc_integer_index_count) ++integer_index_count;
            }
            break;

        case LUA_TTABLE:
            // Avoid recursion on some tables
            if (strcmp(key, "_G") == 0 || strcmp(key, "package.loaded") == 0 || strcmp(level, "__index") == 0) {
                if (GC_LUA_DEBUG)
                    SCP_INFO(()) << "(" << lua_typename(L, lua_type(L, -1)) << ") " << key
                                 << "   (ignored to avoid recursion)";
            } else {
                if (GC_LUA_DEBUG) SCP_INFO(()) << "(table) " << key;
                *next_level++ = '.';
                // CS
                // int int_index_count =
                setParamsFromLuaTable(a_broker, L, -1, next_level);
                // CS
                // if (int_index_count > 0) {
                // sprintf(value, "%d", int_index_count);
                // mApi->setInitValue((std::string(key).substr(0, next_level-key) +
                // "init_size").c_str(), value); if (GC_LUA_VERBOSE) fprintf(stderr,
                // "(SET number) %s = %s\n", (std::string(key).substr(0,
                // next_level-key) + "init_size").c_str(), value);
                //}
            }
            break;

        case LUA_TFUNCTION:
        case LUA_TNIL:
        case LUA_TUSERDATA:
        case LUA_TTHREAD:
        case LUA_TLIGHTUSERDATA:
        default:
            // Ignore other types
            if (GC_LUA_DEBUG) SCP_INFO(()) << "(" << lua_typename(L, lua_type(L, -1)) << ") " << key;
        }

        /* removes 'value'; keeps 'key' for next iteration */
        lua_pop(L, 1);
    }

    return integer_index_count;
}

void gs::LuaFile_Tool::set_g_vars_from_cmdline(lua_State* L, const key_val_args& a_lua_globalvars)
{
    if (a_lua_globalvars.empty()) return;
    for (auto arg : a_lua_globalvars) {
        if (!lua_checkstack(L, 1)) {
            SCP_FATAL(()) << "Can't allocate stack for global variable: " << arg.first;
        }
        if (arg.second == "true") {
            lua_pushboolean(L, 1);
        } else if (arg.second == "false") {
            lua_pushboolean(L, 0);
        } else {
            std::istringstream iss(arg.second);
            lua_Integer int_val = 0;
            lua_Number number_val = 0.0;
            if ((iss >> int_val) && iss.eof()) {
                lua_pushinteger(L, int_val);
            } else {
                iss.clear();
                iss.str(arg.second);
                if ((iss >> number_val) && iss.eof()) {
                    lua_pushnumber(L, number_val);
                } else {
                    lua_pushstring(L, arg.second.c_str());
                }
            }
        }
        lua_setglobal(L, arg.first.c_str());
    }
}
