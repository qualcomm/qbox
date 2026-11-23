# Each RISC-V test ships a paired .S + .ld and gets its firmware built via the
# shared helper defined in tests/qbox/CMakeLists.txt.

# Thin wrapper that pins ARCH=riscv32. Firmware base name is derived from
# the first source file's stem, so call sites only state target, timeout,
# and sources.
function(qbox_add_cpu_riscv32_test target timeout)
    qbox_add_cpu_test(${target} ${timeout} ${ARGN} ARCH riscv32)
endfunction()

qbox_add_cpu_riscv32_test(riscv32-reset-test  100 riscv32-reset.cc)
qbox_add_cpu_riscv32_test(riscv32-dmi-test    100 riscv32-dmi-test.cc)

qbox_add_cpu_riscv32_test(riscv32-irq-test    100 riscv32-irq-test.cc)
target_include_directories(riscv32-irq-test PRIVATE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/qemu-components/timer/riscv_aclint_mtimer/include>)

qbox_add_cpu_riscv32_test(riscv32-qtimer-test 100 riscv32-qtimer-test.cc)
target_include_directories(riscv32-qtimer-test PRIVATE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/qemu-components/timer/riscv_aclint_mtimer/include>)

# This fixture uses one CPU and its sibling CLIC, rather than the two-instance
# CPU matrix. Keep the existing RV32 matrix filter unchanged for other tests.
function(qbox_add_riscv32_clic_test)
    set(QBOX_CPU_TEST_NUM_CPU_COMBINATION 1)
    list(REMOVE_ITEM SKIP_TESTS "riscv.*num-cpu=[^2]")
    qbox_add_cpu_riscv32_test(riscv32-clic-test 100 riscv32-clic-test.cc)
    target_link_libraries(riscv32-clic-test PRIVATE clic_riscv)
endfunction()

qbox_add_riscv32_clic_test()
