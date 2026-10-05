# Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
# SPDX-License-Identifier: BSD-3-Clause

add_library(mcd_client STATIC src/mcd_client.c)
target_include_directories(mcd_client PUBLIC include)
if(WIN32)
    target_link_libraries(mcd_client PUBLIC ws2_32)
endif()

add_library(mcd_debug STATIC src/mcd_debug.cc)
target_include_directories(mcd_debug PUBLIC include)
target_link_libraries(mcd_debug PUBLIC mcd_client)

add_executable(mcd_mcp src/mcd_mcp.cc)
target_link_libraries(mcd_mcp PRIVATE mcd_debug)
