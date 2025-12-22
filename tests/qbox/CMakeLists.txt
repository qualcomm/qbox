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

        # Nonsensical tests
        "hexagon-reset-test.*num-cpu=[^1]"                                        # only works for 1 cpu
        "hexagon.*:accel=(hvf|kvm)"
        "riscv.*:accel=(hvf|kvm)"
        "(SINGLE|COROUTINE).*:accel=(hvf|kvm)"
        "(multithread:|multithread-quantum:|multithread-unconstrained:).*:accel=(hvf|kvm)"
    )


    FILE(GLOB KEYSTONE_PATCH_FILES "${PROJECT_SOURCE_DIR}/patch/keystone/*.patch")

    message(STATUS "Applying patches to keystone: ${KEYSTONE_PATCH_FILES}")

    if(WIN32)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    endif()
    cpmfindpackage(
        NAME keystone
        GIT_REPOSITORY https://github.com/keystone-engine/keystone.git
        GIT_TAG 0.9.2
        GIT_SHALLOW TRUE
        PATCH_COMMAND ${PATCH_EXEC_CMD} ${PATCH_ARGS} "${KEYSTONE_PATCH_FILES}"
        UPDATE_DISCONNECTED 1
        OPTIONS "BUILD_LIBS_ONLY"
    )

function(qbox_add_cpu_test target timeout_s)
    if(NOT ${keystone_FOUND})
        return()
    endif()

    list(APPEND QBOX_TEST_ACCELS "tcg")
    if ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "arm64")
            list(APPEND QBOX_TEST_ACCELS "hvf")
    endif()
    if ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "aarch64")
            list(APPEND QBOX_TEST_ACCELS "kvm")
    endif()

    add_executable(${target} ${ARGN})

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

    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/tests/qbox/include ${keystone_SOURCE_DIR}/include)

    target_link_libraries(${target} PRIVATE cpu_arm_cortexA53 qemu_cpu_hexagon cpu_riscv32 global_peripheral_initiator router reset_gpio gs_memory smmu500 pass exclusive_monitor display plic_sifive ${TARGET_LIBS} keystone hexagon_globalreg)

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
            if (("${threading}" STREQUAL "COROUTINE") AND (NOT (${num_cpu} in WINDOWS_COROUTINE_ALLOWED_NUM_CPU) ) AND (WIN32))
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
endfunction(qbox_add_cpu_test)

add_subdirectory(cpu)

set(QBOX_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR})

function(qbox_extra_add_test target)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE ${QBOX_TEST_DIR}/include)
    target_link_libraries(${target} PRIVATE ${TARGET_LIBS} display)
    add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
endfunction(qbox_extra_add_test)

add_subdirectory(display)
