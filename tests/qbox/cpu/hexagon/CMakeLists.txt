# Hexagon firmwares: assembled with llvm-mc -arch=hexagon, linked with ld.lld,
# objcopied to a raw .bin that the test loads via load_firmware_binary.

# Thin wrapper that pins ARCH=hexagon. Firmware base name is derived from
# the first source file's stem, so call sites only state target, timeout,
# and sources.
function(qbox_add_cpu_hexagon_test target timeout)
    qbox_add_cpu_test(${target} ${timeout} ${ARGN} ARCH hexagon)
endfunction()

qbox_add_cpu_hexagon_test(hexagon-ld-st-mmio-test 100 ld-st-mmio.cc)
qbox_add_cpu_hexagon_test(hexagon-reset-test      100 hexagon-reset.cc)

# load-store-test has a bespoke executable (not a qbox_add_cpu_test caller),
# so its firmware is built explicitly here.
set(HEXAGON_STANDALONE_TEST_NAME "load-store-test")

qbox_build_firmware_bin(${HEXAGON_STANDALONE_TEST_NAME} hexagon ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(
    ${HEXAGON_STANDALONE_TEST_NAME}
    ${HEXAGON_STANDALONE_TEST_NAME}.cc
)

target_link_libraries(
    ${HEXAGON_STANDALONE_TEST_NAME}
    PRIVATE
    ${TARGET_LIBS}
    gs_memory
    router
    qemu_cpu_hexagon
    hexagon_globalreg
)

add_dependencies(${HEXAGON_STANDALONE_TEST_NAME} ${HEXAGON_STANDALONE_TEST_NAME}-firmware)
target_compile_definitions(${HEXAGON_STANDALONE_TEST_NAME} PRIVATE
    FIRMWARE_BIN_PATH="${CMAKE_CURRENT_BINARY_DIR}/${HEXAGON_STANDALONE_TEST_NAME}.bin")

add_test(NAME hexagon-${HEXAGON_STANDALONE_TEST_NAME} COMMAND ${HEXAGON_STANDALONE_TEST_NAME})

# hexagon-smmu-stress-test-v2.s uses V73 ops (tlbw, dmwait, dmlink) that older
# Hexagon LLVM backends (e.g. Ubuntu's stock llvm-18) don't recognise. See
# QQVPQSP-714. Only build it on platforms where a recent-enough Hexagon
# backend ships: macOS via Homebrew's `llvm`, and Windows via MSYS2's
# bundled LLVM.
if(APPLE OR WIN32)
    qbox_add_cpu_hexagon_test(hexagon-smmu-stress-test-v2 100 hexagon-smmu-stress-test-v2.cc
        EXTRA_LD_ARGS --defsym TESTER_ADDR=0x40000 --defsym REG_DEBUG=0x28 --defsym PAGE_SIZE=0x1000)
else()
    message(STATUS "Skipping hexagon-smmu-stress-test-v2: requires recent Hexagon LLVM backend (QQVPQSP-714)")
endif()
