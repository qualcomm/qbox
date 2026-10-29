if(NOT DEFINED GIT_EXECUTABLE OR NOT DEFINED PATCH_DIR)
    message(FATAL_ERROR
        "GIT_EXECUTABLE and PATCH_DIR are required")
endif()

file(GLOB patch_files "${PATCH_DIR}/*.patch")
list(SORT patch_files)

foreach(patch_file IN LISTS patch_files)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --cached --ignore-space-change --check "${patch_file}"
        RESULT_VARIABLE can_apply
        ERROR_VARIABLE apply_error
    )

    if(can_apply EQUAL 0)
        execute_process(
            COMMAND
                "${GIT_EXECUTABLE}"
                -c user.name=qqvp_ci
                -c user.email=qqvp_ci@qualcomm.com
                am --3way "${patch_file}"
            RESULT_VARIABLE am_result
            ERROR_VARIABLE am_error
        )

        if(NOT am_result EQUAL 0)
            message(FATAL_ERROR
                "Could not apply ${patch_file}:\n${am_error}")
        endif()

        continue()
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --cached --ignore-space-change --reverse --check "${patch_file}"
        RESULT_VARIABLE already_applied
    )

    if(already_applied EQUAL 0)
        message(STATUS "Patch already applied: ${patch_file}")
    else()
        message(FATAL_ERROR
            "Patch ${patch_file} cannot be applied and is not already "
            "present. The rpclib source tree is in an incompatible state.\n"
            "${apply_error}")
    endif()
endforeach()
