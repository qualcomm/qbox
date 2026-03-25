cmake_minimum_required(VERSION 3.14)
project(hello-qbox CXX C)

find_package(qbox REQUIRED)

# --- VP binary ------------------------------------------------
add_executable(hello-qbox-vp main.cc)

if(APPLE)
    target_link_options(hello-qbox-vp PRIVATE
        "LINKER:-dead_strip_dylibs")
else()
    target_link_options(hello-qbox-vp PRIVATE
        "LINKER:-as-needed")
endif()

target_link_libraries(hello-qbox-vp PRIVATE qbox::qbox)

# --- Firmware (AArch64 cross-compilation) ---------------------
find_program(AARCH64_GCC aarch64-linux-gnu-gcc)

if(AARCH64_GCC)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/build/hello.elf
        COMMAND ${CMAKE_COMMAND} -E make_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/build
        COMMAND ${AARCH64_GCC}
            -nostdlib -static
            -Ttext=0x80000000 -e _start
            -Wl,--build-id=none
            -Wl,-n
            -o ${CMAKE_CURRENT_SOURCE_DIR}/build/hello.elf
            ${CMAKE_CURRENT_SOURCE_DIR}/hello.c
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/hello.c
        COMMENT "Cross-compiling hello.elf for AArch64"
    )

    add_custom_target(hello-qbox-firmware ALL
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/build/hello.elf)
else()
    message(WARNING
        "aarch64-linux-gnu-gcc not found; "
        "hello.elf must be built manually. "
        "See examples/hello-qbox/README.md.")
endif()
