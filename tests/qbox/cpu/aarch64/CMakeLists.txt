# All AArch64 CPU tests assemble their firmware with llvm-mc + ld.lld and load
# the resulting .bin at runtime (see load_firmware_binary in test/cpu.h).

# Thin wrapper that pins ARCH=aarch64. Firmware base name is derived from
# the first source file's stem, so call sites only state target, timeout,
# and sources.
function(qbox_add_cpu_aarch64_test target timeout)
    qbox_add_cpu_test(${target} ${timeout} ${ARGN} ARCH aarch64)
endfunction()

qbox_add_cpu_aarch64_test(aarch64-simple-write-test          100 simple-write-test.cc)
qbox_add_cpu_aarch64_test(aarch64-dmi-test                   100 dmi-test.cc)
qbox_add_cpu_aarch64_test(aarch64-dmi-test-concurrent-inval  100 dmi-test-concurrent-inval.cc)
qbox_add_cpu_aarch64_test(aarch64-dmi-test-reset             100 dmi-test-reset.cc)
qbox_add_cpu_aarch64_test(aarch64-ld-st-excl-fail-test       100 ld-st-excl-fail.cc)
qbox_add_cpu_aarch64_test(aarch64-write-read                 100 write-read.cc)
qbox_add_cpu_aarch64_test(aarch64-dmi-test-async-inval       100 dmi-test-async-inval.cc)
qbox_add_cpu_aarch64_test(halt-tests                         100 halt-tests.cc)
qbox_add_cpu_aarch64_test(reset-test-system                  100 reset-test-system.cc)
qbox_add_cpu_aarch64_test(reset-test-cpu                     100 reset-test-cpu.cc)
qbox_add_cpu_aarch64_test(aarch64-smmu-router-stress-test-v2 100 smmu-router-stress-test-v2.cc)
