# Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# Reset-stress test: many resets inside one simulation on a real platform
# (router, RAM, uart, loader, reset_gpio, mcd_server), hunting a hang that only
# appears after repeated resets.

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# The VP driver is the example's main.cc: a generic module-factory container.
set(RS_EXAMPLE_DIR ${PROJECT_SOURCE_DIR}/examples/hello-qbox)
set(RS_LUA ${CMAKE_CURRENT_SOURCE_DIR}/reset-stress.lua)

add_executable(reset-stress-vp ${RS_EXAMPLE_DIR}/main.cc)

if(APPLE)
    target_link_options(reset-stress-vp PRIVATE "LINKER:-dead_strip_dylibs")
else()
    target_link_options(reset-stress-vp PRIVATE "LINKER:-as-needed")
endif()

# The topology comes from the lua at runtime, which loads its components as
# dynamic modules by dylib_path.
target_link_libraries(reset-stress-vp PRIVATE ${TARGET_LIBS} cpu_arm_cortexA53)
add_dependencies(reset-stress-vp
    router gs_memory uart-pl011 char_backend_stdio loader mcd_server reset_gpio)

# Firmware: reuse the MCD test's hello-debug.c (zeroes a counter at 0x8ff00000
# then increments it forever, so a restart is observable).
set(RS_FW_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../mcd/hello-debug.c)
set(RS_FW ${CMAKE_CURRENT_BINARY_DIR}/hello-debug.elf)

find_program(AARCH64_GCC aarch64-linux-gnu-gcc)
find_program(CLANG_BIN clang)
find_program(LLD_BIN ld.lld)

set(RS_HAVE_FW FALSE)
if(AARCH64_GCC)
    add_custom_command(
        OUTPUT ${RS_FW}
        COMMAND ${AARCH64_GCC} -nostdlib -static -ffreestanding
                -Ttext=0x80000000 -e _start -Wl,--build-id=none -Wl,-n
                -o ${RS_FW} ${RS_FW_SRC}
        DEPENDS ${RS_FW_SRC}
        COMMENT "Cross-compiling hello-debug.elf for reset-stress (gcc)")
    set(RS_HAVE_FW TRUE)
elseif(CLANG_BIN AND LLD_BIN)
    add_custom_command(
        OUTPUT ${RS_FW}
        COMMAND ${CLANG_BIN} --target=aarch64-elf -ffreestanding -nostdlib
                -c ${RS_FW_SRC} -o ${RS_FW}.o
        COMMAND ${LLD_BIN} ${RS_FW}.o -o ${RS_FW}
                -Ttext=0x80000000 -e _start --build-id=none -n
        DEPENDS ${RS_FW_SRC}
        COMMENT "Cross-compiling hello-debug.elf for reset-stress (clang)")
    set(RS_HAVE_FW TRUE)
endif()

if(RS_HAVE_FW)
    add_custom_target(reset-stress-firmware ALL DEPENDS ${RS_FW})
    add_dependencies(reset-stress-vp reset-stress-firmware)

    # Modest count so CI stays quick; the script takes -n for deep manual runs.
    add_test(
        NAME    reset_stress
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test_reset_stress.py
                -e $<TARGET_FILE:reset-stress-vp>
                -l ${RS_LUA}
                -m $<TARGET_FILE:mcd_mcp>
                -f ${RS_FW}
                -n 50
    )
    # Component dylibs are looked up relative to the VP's working directory.
    set_tests_properties(reset_stress PROPERTIES
        TIMEOUT 900
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
else()
    message(WARNING
        "No AArch64 toolchain (aarch64-linux-gnu-gcc, or clang + ld.lld) found; "
        "the reset-stress test will be skipped.")
endif()
