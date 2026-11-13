if(CMAKE_SCRIPT_MODE_FILE)
    if(NOT DEFINED GIT_EXECUTABLE OR NOT DEFINED PATCH_DIR OR NOT DEFINED PATCH_HASH)
        message(FATAL_ERROR "GIT_EXECUTABLE, PATCH_DIR, and PATCH_HASH are required")
    endif()

    file(GLOB patch_files "${PATCH_DIR}/*.patch")
    list(SORT patch_files)

    foreach(patch_file IN LISTS patch_files)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --ignore-space-change --check "${patch_file}"
            RESULT_VARIABLE can_apply
            ERROR_VARIABLE apply_error
        )

        if(can_apply EQUAL 0)
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" apply --ignore-space-change "${patch_file}"
                COMMAND_ERROR_IS_FATAL ANY
            )
            continue()
        endif()

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --ignore-space-change --reverse --check "${patch_file}"
            RESULT_VARIABLE already_applied
        )

        if(already_applied EQUAL 0)
            message(STATUS "Patch already applied: ${patch_file}")
        else()
            message(FATAL_ERROR "Patch ${patch_file} cannot be applied and is not already present.\n${apply_error}")
        endif()
    endforeach()
else()
    set_property(GLOBAL PROPERTY QBOX_GIT_PATCH_SCRIPT "${CMAKE_CURRENT_LIST_FILE}")

    function(qbox_git_patch_command output patch_dir)
        find_package(Git REQUIRED)

        file(GLOB _patch_files CONFIGURE_DEPENDS LIST_DIRECTORIES false "${patch_dir}/*.patch")
        list(SORT _patch_files)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_patch_files})
        set(_patch_manifest)

        foreach(_patch_file IN LISTS _patch_files)
            file(SHA256 "${_patch_file}" _patch_hash)
            string(APPEND _patch_manifest "${_patch_file}:${_patch_hash}\n")
        endforeach()
        string(SHA256 _patch_hash "${_patch_manifest}")

        get_property(_patch_script GLOBAL PROPERTY QBOX_GIT_PATCH_SCRIPT)
        set(${output}
            "${CMAKE_COMMAND}"
            "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
            "-DPATCH_DIR=${patch_dir}"
            "-DPATCH_HASH=${_patch_hash}"
            -P "${_patch_script}"
            PARENT_SCOPE
        )
    endfunction()
endif()
