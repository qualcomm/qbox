# Place test executables under <build>/tests/... mirroring the source tree,
# rather than in the build root.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "")

# On Windows there is no RPATH, so test executables need the build root on
# PATH in order to find qbox.dll and libqemu.
if(WIN32)
    set(_test_env_mod "PATH=path_list_prepend:${CMAKE_BINARY_DIR}")
    macro(add_test)
        _add_test(${ARGN})
        cmake_parse_arguments(_gs_at "" "NAME" "" ${ARGN})
        if(_gs_at_NAME)
            set_tests_properties(${_gs_at_NAME} PROPERTIES
                ENVIRONMENT_MODIFICATION "${_test_env_mod}")
        endif()
    endmacro()
endif()

add_subdirectory(components)
add_subdirectory(sync)
add_subdirectory(utils)
if(NOT WITHOUT_QEMU)
    add_subdirectory(qbox)
    add_subdirectory(subsys)
endif()
