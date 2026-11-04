# Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
# In-tree tests for the MCD debug server and its MCP bridge: built and run as
# part of the normal `ctest` run.

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# The VP driver is the example's main.cc: a generic module-factory container.
set(MCD_EXAMPLE_DIR ${PROJECT_SOURCE_DIR}/examples/hello-qbox)
set(MCD_LUA ${CMAKE_CURRENT_SOURCE_DIR}/mcd-platform.lua)

# The topology comes entirely from the lua file at runtime.
add_executable(mcd-vp ${MCD_EXAMPLE_DIR}/main.cc)

if(APPLE)
    target_link_options(mcd-vp PRIVATE "LINKER:-dead_strip_dylibs")
else()
    target_link_options(mcd-vp PRIVATE "LINKER:-as-needed")
endif()

# The lua loads its components as dynamic modules by dylib_path, so mcd-vp links
# only the core library plus the AArch64 CPU support, and depends on the
# dynamically-loaded modules so they exist before the test runs.
target_link_libraries(mcd-vp PRIVATE ${TARGET_LIBS} cpu_arm_cortexA53)
add_dependencies(mcd-vp
    router gs_memory uart-pl011 char_backend_stdio loader mcd_server)

# Debug firmware (spins forever, so the target stays live for the debugger).
# Prefer the AArch64 cross-gcc, else clang with an AArch64 target plus ld.lld;
# with neither toolchain the integration tests are skipped.
set(DEBUG_FW_SRC ${CMAKE_CURRENT_SOURCE_DIR}/hello-debug.c)
set(DEBUG_FW ${CMAKE_CURRENT_BINARY_DIR}/hello-debug.elf)

find_program(AARCH64_GCC aarch64-linux-gnu-gcc)
find_program(CLANG_BIN clang)
find_program(LLD_BIN ld.lld)

set(HAVE_DEBUG_FW FALSE)
if(AARCH64_GCC)
    add_custom_command(
        OUTPUT ${DEBUG_FW}
        COMMAND ${AARCH64_GCC} -nostdlib -static -ffreestanding
                -Ttext=0x80000000 -e _start -Wl,--build-id=none -Wl,-n
                -o ${DEBUG_FW} ${DEBUG_FW_SRC}
        DEPENDS ${DEBUG_FW_SRC}
        COMMENT "Cross-compiling hello-debug.elf (gcc)")
    set(HAVE_DEBUG_FW TRUE)
elseif(CLANG_BIN AND LLD_BIN)
    add_custom_command(
        OUTPUT ${DEBUG_FW}
        COMMAND ${CLANG_BIN} --target=aarch64-elf -ffreestanding -nostdlib
                -c ${DEBUG_FW_SRC} -o ${DEBUG_FW}.o
        COMMAND ${LLD_BIN} ${DEBUG_FW}.o -o ${DEBUG_FW}
                -Ttext=0x80000000 -e _start --build-id=none -n
        DEPENDS ${DEBUG_FW_SRC}
        COMMENT "Cross-compiling hello-debug.elf (clang)")
    set(HAVE_DEBUG_FW TRUE)
endif()

# Pure-software unit tests (JSON + wire framing) need no firmware or VP.
add_test(
    NAME    mcd_mcp_unit
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_mcd_unit.py
            -m $<TARGET_FILE:mcd_mcp>
)
set_tests_properties(mcd_mcp_unit PROPERTIES TIMEOUT 60)

# End-to-end: boots mcd-vp with the debug firmware and drives it through mcd_mcp.
if(HAVE_DEBUG_FW)
    add_custom_target(hello-debug-firmware ALL DEPENDS ${DEBUG_FW})
    add_dependencies(mcd-vp hello-debug-firmware)

    add_test(
        NAME    mcd_mcp_hello
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test_mcd_mcp.py
                -e $<TARGET_FILE:mcd-vp>
                -l ${MCD_LUA}
                -m $<TARGET_FILE:mcd_mcp>
                -f ${DEBUG_FW}
    )
    # Component dylibs are looked up relative to the VP's working directory, so
    # run from the build root where the dylibs live.
    set_tests_properties(mcd_mcp_hello PROPERTIES
        TIMEOUT 120
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

    # Multi-core: the same platform with two CPUs on one QemuInstance.
    add_test(
        NAME    mcd_mcp_smp
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test_mcd_smp.py
                -e $<TARGET_FILE:mcd-vp>
                -l ${MCD_LUA}
                -m $<TARGET_FILE:mcd_mcp>
                -f ${DEBUG_FW}
    )
    set_tests_properties(mcd_mcp_smp PROPERTIES
        TIMEOUT 180
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
else()
    message(WARNING
        "No AArch64 toolchain (aarch64-linux-gnu-gcc, or clang + ld.lld) found; "
        "the MCD integration test (mcd_mcp_hello) will be skipped.")
endif()
