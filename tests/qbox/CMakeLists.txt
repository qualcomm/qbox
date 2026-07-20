list(APPEND QBOX_CPU_TEST_SYNC_POLICY_COMBINATION
    tlm2
    tlm2-icount
    multithread
    multithread-quantum
    multithread-unconstrained
    multithread-single
    )

list(APPEND QBOX_CPU_TEST_NUM_CPU_COMBINATION
    1
    2
    4
    # 8
    # 16
    32
    # 64
    # 128
    )

    list(APPEND SKIP_TESTS
        "riscv.*num-cpu=[^2]"                                                     # reduce redundency
        "hexagon.*num-cpu=[^14]"                                                  # reduce redundency
        "hexagon-smmu-stress-test-v2:.*num-cpu=[^1].*:threading=COROUTINE"        # takes too long to execute
        "hexagon-smmu-stress-test-v2:.*num-cpu=[^1].*:threading=MULTI"            # takes too long to execute
        "hexagon-smmu-stress-test-v2:.*num-cpu=[^1].*:threading=SINGLE"           # takes too long to execute
#        "num-cpu=32:.*:accel=hvf"                                                 # takes too long
#        "num-cpu=32:.*:accel=kvm"                                                 #    Due to sync issues.

        # Bugs?
        "halt-tests:.*num-cpu=[^1].*:threading=COROUTINE"                         # hangs during init
        "hexagon-ld-st-mmio-test:.*num-cpu=[^1].*:threading=COROUTINE"            # hangs during init
        "hexagon-ld-st-mmio-test:.*num-cpu=[^1].*:threading=SINGLE"               # hangs during init
        "aarch64-ld-st-excl-fail-test:.*:accel=(hvf|kvm)"                         # Test fails due to ld/st ex
        "reset-test-cpu:.*:accel=hvf"                                             # Test fails
        "aarch64-smmu-router-stress-test-v2:.*:num-cpu=1:.*:accel=hvf"            # Doesn't start with only one CPU
        "aarch64-dmi-test-reset:.*num-cpu=(4|32)"                                 # Intermittent SEGFAULT on Ubuntu (flaky, num-cpu>2)

        # Nonsensical tests
        "hexagon-reset-test.*num-cpu=[^1]"                                        # only works for 1 cpu
        "hexagon.*:accel=(hvf|kvm)"
        "riscv.*:accel=(hvf|kvm)"
        "(SINGLE|COROUTINE).*:accel=(hvf|kvm)"
        "(multithread:|multithread-quantum:|multithread-unconstrained:).*:accel=(hvf|kvm)"
    )


    # LLVM toolchain is used to assemble firmware binaries at configure time
    find_program(LLVM_MC llvm-mc HINTS /opt/homebrew/opt/llvm/bin)
    find_program(LLD ld.lld HINTS /opt/homebrew/opt/llvm/bin)
    find_program(LLVM_OBJCOPY llvm-objcopy HINTS /opt/homebrew/opt/llvm/bin)

    if(NOT LLVM_MC OR NOT LLD OR NOT LLVM_OBJCOPY)
        message(FATAL_ERROR
            "Qbox CPU tests require the LLVM toolchain (llvm-mc, ld.lld, llvm-objcopy) "
            "to assemble test firmware. Run scripts/install_dependencies.sh to install "
            "all required dependencies, then re-run cmake.\n"
            "  llvm-mc: ${LLVM_MC}\n  ld.lld: ${LLD}\n  llvm-objcopy: ${LLVM_OBJCOPY}")
    endif()

    # qbox_build_firmware_bin(<name> <arch> <src_dir> [EXTRA_LD_ARGS ...])
    #
    # Assembles <src_dir>/<name>.S with llvm-mc, links it with ld.lld using
    # <src_dir>/<name>.ld, and objcopies the result to a raw .bin. Creates a
    # custom target <name>-firmware that depends on the .bin.
    #
    # <arch> is passed to `llvm-mc -arch=` (e.g. aarch64, hexagon, riscv32).
    # EXTRA_LD_ARGS can supply additional `ld.lld` flags such as `--defsym`.
    function(qbox_build_firmware_bin name arch src_dir)
        cmake_parse_arguments(FW "" "" "EXTRA_LD_ARGS" ${ARGN})

        # Accept either name.S or name.s
        if(EXISTS "${src_dir}/${name}.S")
            set(src "${src_dir}/${name}.S")
        elseif(EXISTS "${src_dir}/${name}.s")
            set(src "${src_dir}/${name}.s")
        else()
            message(FATAL_ERROR "qbox_build_firmware_bin: no ${name}.S or ${name}.s in ${src_dir}")
        endif()

        # Prefer a per-firmware linker script; fall back to a shared firmware.ld
        # in the same directory so tests that share the same layout don't need
        # a duplicated .ld file each.
        if(EXISTS "${src_dir}/${name}.ld")
            set(ld_script "${src_dir}/${name}.ld")
        elseif(EXISTS "${src_dir}/firmware.ld")
            set(ld_script "${src_dir}/firmware.ld")
        else()
            message(FATAL_ERROR "qbox_build_firmware_bin: no ${name}.ld or firmware.ld in ${src_dir}")
        endif()
        set(obj "${CMAKE_CURRENT_BINARY_DIR}/${name}.o")
        set(elf "${CMAKE_CURRENT_BINARY_DIR}/${name}.elf")
        set(bin "${CMAKE_CURRENT_BINARY_DIR}/${name}.bin")

        set(mc_triple_args "-arch=${arch}" -filetype=obj)
        if("${arch}" STREQUAL "aarch64")
            list(APPEND mc_triple_args -triple=aarch64-none-elf)
            set(lld_emulation aarch64elf)
        elseif("${arch}" STREQUAL "hexagon")
            set(lld_emulation hexagonelf)
        elseif("${arch}" STREQUAL "riscv32")
            set(lld_emulation elf32lriscv)
        elseif("${arch}" STREQUAL "riscv64")
            set(lld_emulation elf64lriscv)
        else()
            message(FATAL_ERROR "qbox_build_firmware_bin: unsupported arch '${arch}'")
        endif()

        # On MSYS2/Windows, ld.lld defaults to PE-COFF and ignores -flavor gnu,
        # so an explicit -m <emulation> is required to force ELF/GNU mode.
        # Linux/macOS ld.lld already defaults to ELF and older LLDs (e.g.
        # Ubuntu's llvm-18) don't even know some of the ELF emulation names
        # (hexagonelf lands first), so we only pass -m on Windows.
        if(WIN32)
            set(lld_mode_args -m ${lld_emulation})
        else()
            set(lld_mode_args)
        endif()

        get_filename_component(src_name "${src}" NAME)
        add_custom_command(
            OUTPUT ${obj}
            COMMAND ${LLVM_MC} ${mc_triple_args} ${src} -o ${obj}
            DEPENDS ${src}
            COMMENT "Assembling ${src_name}"
        )

        add_custom_command(
            OUTPUT ${elf}
            COMMAND ${LLD} ${lld_mode_args} -T ${ld_script} ${obj} ${FW_EXTRA_LD_ARGS} -o ${elf}
            DEPENDS ${obj} ${ld_script}
            COMMENT "Linking ${name}.elf"
        )

        add_custom_command(
            OUTPUT ${bin}
            COMMAND ${LLVM_OBJCOPY} -O binary ${elf} ${bin}
            DEPENDS ${elf}
            COMMENT "Creating ${name}.bin"
        )

        add_custom_target(${name}-firmware DEPENDS ${bin})
    endfunction()

    # qbox_attach_firmware(<target> <fw_name>)
    #
    # Wires a pre-built firmware binary (produced by qbox_build_firmware_bin)
    # to a test executable: adds the build dependency and defines
    # FIRMWARE_BIN_PATH so the test knows where to mmap/load the .bin at runtime.
    function(qbox_attach_firmware target fw_name)
        add_dependencies(${target} ${fw_name}-firmware)
        target_compile_definitions(${target} PRIVATE
            FIRMWARE_BIN_PATH="${CMAKE_CURRENT_BINARY_DIR}/${fw_name}.bin")
    endfunction()

    # qbox_add_cpu_test(<target> <timeout_s> <sources>...
    #                   [ARCH <arch>]
    #                   [FIRMWARE <name>]
    #                   [FIRMWARE_SRC_DIR <dir>]
    #                   [EXTRA_LD_ARGS <args>...])
    #
    # Builds (via qbox_build_firmware_bin) and attaches a firmware binary to
    # the test. By default the firmware base name is derived from the first
    # source file: <first-source>.cc → firmware <first-source>, so .S/.ld
    # files share that stem. Pass FIRMWARE <name> only to override the
    # default (e.g. when sharing firmware across tests). ARCH is required
    # whenever a firmware target needs to be built. FIRMWARE_SRC_DIR
    # defaults to CMAKE_CURRENT_SOURCE_DIR. EXTRA_LD_ARGS is forwarded to
    # ld.lld.
function(qbox_add_cpu_test target timeout_s)
    cmake_parse_arguments(QBT "" "ARCH;FIRMWARE;FIRMWARE_SRC_DIR" "EXTRA_LD_ARGS" ${ARGN})
    set(test_sources ${QBT_UNPARSED_ARGUMENTS})

    set(firmware "${QBT_FIRMWARE}")
    if(NOT firmware)
        list(GET test_sources 0 _first_source)
        get_filename_component(firmware "${_first_source}" NAME_WE)
    endif()

    if(NOT TARGET ${firmware}-firmware)
        if(NOT QBT_ARCH)
            message(FATAL_ERROR
                "qbox_add_cpu_test(${target}): ARCH is required to build firmware '${firmware}'")
        endif()
        set(_fw_src_dir "${QBT_FIRMWARE_SRC_DIR}")
        if(NOT _fw_src_dir)
            set(_fw_src_dir "${CMAKE_CURRENT_SOURCE_DIR}")
        endif()
        set(_extra_ld_args)
        if(QBT_EXTRA_LD_ARGS)
            set(_extra_ld_args EXTRA_LD_ARGS ${QBT_EXTRA_LD_ARGS})
        endif()
        qbox_build_firmware_bin(${firmware} ${QBT_ARCH} ${_fw_src_dir} ${_extra_ld_args})
    endif()

    list(APPEND QBOX_TEST_ACCELS "tcg")
    if ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "arm64")
            list(APPEND QBOX_TEST_ACCELS "hvf")
    endif()
    if ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "aarch64")
            list(APPEND QBOX_TEST_ACCELS "kvm")
    endif()

    add_executable(${target} ${test_sources})

    # Apply HVF entitlements on MacOS
    if(APPLE)
        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMAND codesign
                --entitlements ${libqemu_SOURCE_DIR}/accel/hvf/entitlements.plist
                --force -s - $<TARGET_FILE:${target}>
            COMMENT "Applying HVF entitlements to $<TARGET_FILE:${target}>"
        )
    endif()

    #To disable MCIPS tests: pass -DQBOX_ENABLE_MCIPS_TESTS=OFF at configure time
    option(QBOX_ENABLE_MCIPS_TESTS "Enable mcips=true test variants" ON)

    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/tests/qbox/include)

    target_link_libraries(${target}
        PRIVATE
        cpu_arm_cortexA53 qemu_cpu_hexagon cpu_riscv32
        global_peripheral_initiator router reset_gpio gs_memory smmu500 pass
        exclusive_monitor display plic_sifive qbox-common hexagon_globalreg
    )

    foreach (accel ${QBOX_TEST_ACCELS})
      foreach(sync_pol ${QBOX_CPU_TEST_SYNC_POLICY_COMBINATION})
        if("${sync_pol}" STREQUAL "tlm2-icount")
            if (${accel} STREQUAL "tcg")
                set(sync_pol "tlm2")
                set(icount "true")
            else()
                continue()
            endif()
        else()
            set(icount "false")
        endif()

        if("${sync_pol}" STREQUAL "multithread-single")
            if (${accel} STREQUAL "tcg")
                set(sync_pol "multithread")
                set(threading "SINGLE")
            else()
                continue()
            endif()
        else ()
            set(threading "MULTI")
        endif()

        if("${sync_pol}" STREQUAL "tlm2")
            if (NOT ("${target}" STREQUAL "hexagon-reset-test"))
                set(threading "COROUTINE")
            else()
                continue()
            endif()
        endif()

        # Only generate mcips=true when threading=MULTI and accel=tcg.
        # For COROUTINE or SINGLE (or non-tcg), only mcips=false is generated.
        set(mcips_variants false)
        if(QBOX_ENABLE_MCIPS_TESTS AND "${threading}" STREQUAL "MULTI" AND "${accel}" STREQUAL "tcg")
            set(mcips_variants true false)
        endif()

        foreach(mcips ${mcips_variants})
          if(${mcips})
              set(time_sync_strategy "mcips")
          else()
              set(time_sync_strategy "quantum_keeper")
          endif()
          foreach(num_cpu ${QBOX_CPU_TEST_NUM_CPU_COMBINATION})
            set(test_name ${target}:sync-pol=${sync_pol}:num-cpu=${num_cpu}:icount=${icount}:threading=${threading}:accel=${accel}:time_sync_strategy=${time_sync_strategy})

            set (skip FALSE)
            foreach(tgt_name IN LISTS SKIP_TESTS)
                if("${test_name}" MATCHES "${tgt_name}")
                    set(skip TRUE)
                    break()
                endif()
            endforeach()
            if (${skip})
                continue()
            endif()

            # On Windows, coroutine mode is only supported with 1 or 2 CPUs
            set(WINDOWS_COROUTINE_ALLOWED_NUM_CPU 1 2)
            if (("${threading}" STREQUAL "COROUTINE") AND (WIN32))
               continue()
            endif()

            add_test(
                NAME ${test_name}
                COMMAND $<TARGET_FILE:${target}> -p test-bench.inst_a.sync_policy=${sync_pol}
                                                 -p test-bench.inst_b.sync_policy=${sync_pol}
                                                 -p test-bench.inst_a.accel=${accel}
                                                 -p test-bench.inst_b.accel=tcg
                                                 -p test-bench.num_cpu=${num_cpu}
                                                 -p test-bench.inst_a.icount=${icount}
                                                 -p test-bench.inst_b.icount=${icount}
                                                 $<$<STREQUAL:${icount},true>:-p test-bench.inst_a.icount_mips_shift=3>
                                                 $<$<STREQUAL:${icount},true>:-p test-bench.inst_b.icount_mips_shift=3>
                                                 -p test-bench.inst_a.tcg_mode=${threading}
                                                 -p test-bench.inst_b.tcg_mode=${threading}

                                                 -p test-bench.inst_a.time_sync_strategy=${time_sync_strategy}
                                                 -p test-bench.inst_b.time_sync_strategy=${time_sync_strategy}

                                                 -p test-bench.bulkmem.max_block_size=1024
                                                 -p test-bench.bulkmem.min_block_size=32

                                                 -p log_level=0
                                                 )
            set_tests_properties(${test_name} PROPERTIES TIMEOUT ${timeout_s})
            if (GS_ENABLE_SANITIZERS)
                # Please see this issue https://bugzilla.redhat.com/show_bug.cgi?id=1449604
                set_tests_properties(${test_name} PROPERTIES
                    ENVIRONMENT LD_LIBRARY_PATH="$ENV{LD_LIBRARY_PATH}":$ENV{LD_LIBRARY_PATH})
            endif()
            # Alternative solution until the problem is solved with the exclusive access tests
            # --------------------------------------------------------------------------------
            if(("${target}" STREQUAL "aarch64-ld-st-excl-fail-test") AND ( ("${sync_pol}" STREQUAL "tlm2") OR ("${threading}" STREQUAL "SINGLE") ) )
                set_tests_properties(${test_name} PROPERTIES WILL_FAIL TRUE)
            endif()
            # --------------------------------------------------------------------------------
          endforeach()
        endforeach()
      endforeach()
    endforeach()

    qbox_attach_firmware(${target} ${firmware})
endfunction(qbox_add_cpu_test)

add_subdirectory(cpu)

set(QBOX_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR})

function(qbox_extra_add_test target)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE ${QBOX_TEST_DIR}/include)
    target_link_libraries(${target} PRIVATE qbox-common gtest gmock display)
    add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
endfunction(qbox_extra_add_test)

add_subdirectory(display)
