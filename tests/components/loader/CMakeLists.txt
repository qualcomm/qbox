macro(gs_add_test test)
    add_executable(${test} ${test}.cc)
     target_link_libraries(${test} PRIVATE gtest gmock router gs_memory qbox-common)
    add_test(NAME ${test} COMMAND ${test} --gs_luafile ${CMAKE_CURRENT_SOURCE_DIR}/conf-test.lua)

    if (GS_ENABLE_SANITIZERS)
        set(ENV{ASAN_OPTIONS} "detect_leaks=1")
        set(ENV{LSAN_OPTIONS} "suppressions=${CMAKE_CURRENT_SOURCE_DIR}/ignore_leaks.txt")
        set_tests_properties(${test} PROPERTIES
            ENVIRONMENT ASAN_OPTIONS="detect_leaks=1":$ENV{ASAN_OPTIONS}
            ENVIRONMENT LSAN_OPTIONS="suppressions=${CMAKE_CURRENT_SOURCE_DIR}/ignore_leaks.txt":$ENV{LSAN_OPTIONS})
    endif()

    set_tests_properties(${test} PROPERTIES TIMEOUT 30)
endmacro()

gs_add_test(loader-test)
