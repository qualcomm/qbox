/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __GS_ARG_PARSER_H__
#define __GS_ARG_PARSER_H__

#include <scp/report.h>
#include <cci_configuration>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <scp/report.h>
#include <getopt.h>
#include <memory>
#include "luafile_tool.h"

/**
 * This is an example VP command line arguments parser to call the proper APIs of the
 * LuaFile_Tool class to configure a platform and populate CCI parameters.
 */

class ArgParser
{
    SCP_LOGGER();

public:
    ArgParser(cci::cci_broker_handle a_broker, const int argc, char* const argv[], bool enforce_config_file = false)
        : lua()
    {
        SCP_DEBUG(()) << "ArgParser Constructor";
        parseCommandLineWithGetOpt(a_broker, argc, argv, enforce_config_file);
    }

protected:
    /// convert "foo=bar" kind of command line arg to std::pair<std::string, std::string>
    /**
     *
     * @param arg The "foo=bar" cmd line argument.
     * @return std::pair<std::string, std::string> key-value pair of the cmd line argument.
     */
    std::pair<std::string, std::string> get_key_val_args(const std::string& arg)
    {
        std::stringstream ss(arg);
        std::string key, value;

        if (!std::getline(ss, key, '=')) {
            SCP_FATAL(()) << "parameter name not found!" << std::endl;
        }
        if (!std::getline(ss, value)) {
            SCP_FATAL(()) << "parameter value not found!" << std::endl;
        }

        // If the value doesn't look like valid JSON, treat it as a string
        // by wrapping it in quotes. This allows command lines like:
        //   -p key=multithread    (instead of -p key=\"multithread\")
        // which makes CTest's "Command:" log line copy-pasteable.
        if (!value.empty() && value[0] != '"' && value[0] != '[' && value[0] != '{' && value != "true" &&
            value != "false" && value != "null" && !std::isdigit(value[0]) && value[0] != '-') {
            value = "\"" + value + "\"";
        }

        SCP_INFO(()) << "Setting param " << key << " to value " << value;

        return std::make_pair(key, value);
    }

    /// Parses the command line with getopt and extracts the luafile option.
    /**
     *
     * @param argc The argc of main(...).
     * @param argv The argv of main(...).
     */
    void parseCommandLineWithGetOpt(cci::cci_broker_handle a_broker, const int argc, const char* const* argv,
                                    bool enforce_config_file)
    {
        SCP_INFO(()) << "Parse command line for --gs_luafile option (" << argc << " arguments)";

        bool luafile_found = false;
        char* images_dir = NULL;
        // getopt permutes argv array, so copy it to argv_cp
        std::vector<std::unique_ptr<char[]>> argv_cp;
        char* argv_cp_raw[argc];
        for (int i = 0; i < argc; i++) {
            size_t len = strlen(argv[i]) + 1; // count \0
            argv_cp.emplace_back(std::make_unique<char[]>(len));
            strcpy(argv_cp[i].get(), argv[i]);
            argv_cp_raw[i] = argv_cp[i].get();
        }

        // configure getopt
        optind = 0; // reset of getopt
        opterr = 0; // avoid error message for not recognized option

        // Don't add 'i' here. It must be specified as a long option.
        const char* optstring = "l:p:";

        struct option long_options[] = { { "gs_luafile", required_argument, 0, 'l' }, // '--luafile filename'
                                         { "param", required_argument, 0, 'p' },      // --param foo.baa=10
                                         { "images-dir", required_argument, 0, 'i' }, // --images-dir path
                                         { 0, 0, 0, 0 } };

        // parse --images-dir first to make it available to every lua config
        while (1) {
            int c = getopt_long(argc, argv_cp_raw, optstring, long_options, 0);
            if (c == EOF) break;
            if (c == 'i') images_dir = optarg;
        }
        optind = 0; // reset of getopt

        while (1) {
            int c = getopt_long(argc, argv_cp_raw, optstring, long_options, 0);
            if (c == EOF) break;

            switch (c) {
            case 'l': // -l and --gs_luafile
            {
                SCP_INFO(()) << "Option --gs_luafile with value " << optarg;
                SCP_INFO(()) << "Lua file command line parser: parse option --gs_luafile " << optarg << std::endl;
                lua.config(a_broker, optarg, images_dir);
                luafile_found = true;
                break;
            }

            case 'p': // -p and --param
            {
                auto param = get_key_val_args(std::string(optarg));
                a_broker.set_preset_cci_value(param.first, cci::cci_value(cci::cci_value::from_json(param.second)));
                break;
            }

            default:
                /* unrecognized option */
                break;
            }
        }

        if (enforce_config_file && !luafile_found) {
            SCP_FATAL(()) << "fatal: missing required --gs_luafile argument";
        }
    }

private:
    gs::LuaFile_Tool lua;
};

#endif