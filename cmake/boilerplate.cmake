set(_BOILERPLATE_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "")

macro(gs_addexpackage)
  # The purpose of 'GS_ONLY' is to provides a tarball without any sources EXCEPT the boilerplate and QEMU
  if (NOT GS_ONLY)
    cpmaddpackage(${ARGV})
  endif()
endmacro()

# ##############################################################################
# ----- SystemC and CCI Dependencies
# ##############################################################################

macro(install_systemc)
    if(DEFINED ENV{SYSTEMC_HOME} OR DEFINED SYSTEMC_HOME)
        set(SYSTEMC_HOME $ENV{SYSTEMC_HOME})

        CPMAddPackage(NAME SystemC SOURCE_DIR ${SYSTEMC_HOME})

        set(SystemCLanguage_FOUND TRUE)
        set(SystemCLanguageLocal_FOUND TRUE)
    else()
        gs_addexpackage(
            NAME SystemCLanguage
            GIT_REPOSITORY ${SYSTEMC_GIT}
            GIT_TAG febde7a3e007ce3030cf574a4cb6fcc13d0ed820
            GIT_SHALLOW OFF
            OPTIONS "ENABLE_SUSPEND_ALL" "ENABLE_PHASE_CALLBACKS"
        )

        # Prevent CCI attempting to re-find SystemC
        if(SystemCLanguage_ADDED)
            set(SystemCLanguage_FOUND TRUE)
        endif()

        # upstream set INSTALL_NAME_DIR wrong !
        if(APPLE)
            set_target_properties(
                systemc
                PROPERTIES
                INSTALL_NAME_DIR "@rpath"
            )
        endif()

        message(STATUS "Using SystemC ${SystemCLanguage_VERSION} (${SystemCLanguage_SOURCE_DIR})")
    endif()
endmacro()

macro(install_systemc_dependencies)

    gs_addexpackage(
        NAME SystemCCCI
        GIT_REPOSITORY ${CCI_GIT}
        # If GIT_SHALLOW is enabled then GIT_TAG works only with branch names
        # and tags. A commit hash is not allowed.
        GIT_TAG aff681775057a9a332983b240f27bb4e6aae3887
        GIT_SHALLOW OFF
        OPTIONS "SYSTEMCCCI_BUILD_TESTS OFF"
    )

    gs_addexpackage(
        NAME SCP
        GIT_REPOSITORY ${SCP_GIT}
        GIT_TAG 6830c915bb691d9b505b17ac631f1ff305fe9c17
        GIT_SHALLOW OFF
    )
endmacro()

macro(configure_systemc)
    if(SystemCCCI_ADDED)
        set(SystemCCCI_FOUND TRUE)
    endif()

    # upstream set INSTALL_NAME_DIR wrong !
    if(APPLE)
        set_target_properties(
            cci-config
            PROPERTIES
            INSTALL_NAME_DIR "@rpath"
        )
    endif()

    message(STATUS "Using SystemCCI ${SystemCCCI_VERSION} (${SystemCCCI_SOURCE_DIR})")

    set(SYSTEMC_PROJECT TRUE)

    add_compile_definitions(HAS_CCI)

    if(NOT GS_ONLY)
        list(APPEND TARGET_LIBS SystemC::systemc SystemC::cci-config scp::reporting)
        set(TARGET_LIBS "${TARGET_LIBS}" CACHE INTERNAL "target_libs")
    endif()
endmacro()

macro(gs_systemc)
    install_systemc()
    install_systemc_dependencies()
    configure_systemc()
endmacro()
# ##############################################################################

macro(gs_create_dymod MODULE_NAME)
  file(GLOB_RECURSE SOURCE_COMPONENT "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")

  add_library(${MODULE_NAME} SHARED
              ${SOURCE_COMPONENT})

  # Define where to install the dynamic library during building
  set_target_properties(${MODULE_NAME} PROPERTIES
                        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
                        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

  # Avoid to have the "lib" prefix on the name of the library
  set_target_properties(${MODULE_NAME} PROPERTIES PREFIX "")

  if ( TARGET_LIBS )
    add_dependencies(${MODULE_NAME} ${TARGET_LIBS})
    target_link_libraries(${MODULE_NAME} PUBLIC
        ${TARGET_LIBS}
    )
  endif()

  if (WIN32)
    target_link_libraries(${MODULE_NAME} PRIVATE ws2_32 mswsock)

    # When a dymod links against another dymod, the symbol `module_register`
    # of the imported dymod shadows the same symbol of the importee.
    # The .def file forces the export of the local module_register, and
    # --export-all-symbols ensures every other symbol remains exported too
    # (without it, the .def file restricts exports to only module_register,
    # breaking test executables that link against the import library).
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${MODULE_NAME}_exports.def"
        "EXPORTS\n    module_register\n")
    target_sources(${MODULE_NAME} PRIVATE
                  "${CMAKE_CURRENT_BINARY_DIR}/${MODULE_NAME}_exports.def")
    target_link_options(${MODULE_NAME} PRIVATE -Wl,--export-all-symbols)
  endif()

  foreach(T IN LISTS TARGET_LIBS)
    target_include_directories(
      ${MODULE_NAME} PUBLIC
      $<TARGET_PROPERTY:${T},INTERFACE_INCLUDE_DIRECTORIES>
    )
  endforeach()

  target_include_directories(
    ${MODULE_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_PREFIX}>
  )

  # If hex/json source files exist, set up a rule to build the config zip.
  # The zip is rebuilt whenever the hex or json sources change.
  # The python script handles hex->bin conversion and zip packing.
  file(GLOB _hex_files "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hex")
  file(GLOB _json_files "${CMAKE_CURRENT_SOURCE_DIR}/src/*.json")
  if(_hex_files OR _json_files)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    list(GET SOURCE_COMPONENT 0 _first_src)
    get_filename_component(_src_name ${_first_src} NAME)
    set(_zip "${CMAKE_CURRENT_SOURCE_DIR}/src/${_src_name}_config.zip")

    add_custom_command(
      OUTPUT ${_zip}
      COMMAND ${Python3_EXECUTABLE} ${_BOILERPLATE_DIR}/make_config_zip.py
              ${_zip} ${_json_files} ${_hex_files}
      DEPENDS ${_json_files} ${_hex_files}
      COMMENT "${MODULE_NAME}: building config zip from hex/json sources"
    )
    add_custom_target(${MODULE_NAME}_config DEPENDS ${_zip})
    add_dependencies(${MODULE_NAME} ${MODULE_NAME}_config)
    # Ensure the source that INCBINs the zip is recompiled when it changes
    set_property(SOURCE ${_first_src} APPEND PROPERTY OBJECT_DEPENDS ${_zip})
  endif()

  install(
    TARGETS ${MODULE_NAME}
    # we export the ${MODULE_NAME}, like that we can use ${MODULE_NAME} with qqvp.
    EXPORT qqvpTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT "${LIBRARY_NAME}_Runtime"
            NAMELINK_COMPONENT "${LIBRARY_NAME}_Development"
  )

endmacro()

# ##############################################################################
# ----- LUA
# ##############################################################################
macro(build_lua)
  if (lua_ADDED)
      # lua has no CMake support, so we create our own target
      FILE(GLOB lua_sources ${lua_SOURCE_DIR}/*.c)
      list(REMOVE_ITEM lua_sources "${lua_SOURCE_DIR}/lua.c" "${lua_SOURCE_DIR}/luac.c" "${lua_SOURCE_DIR}/ltests.c" "${lua_SOURCE_DIR}/onelua.c")
      add_library(lua SHARED ${lua_sources})
      if(NOT WIN32)
        target_compile_definitions(lua PRIVATE LUA_USE_POSIX)
      endif()
      target_include_directories(lua PUBLIC
          $<BUILD_INTERFACE:${lua_SOURCE_DIR}>
          $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
      )
      target_compile_definitions(${PROJECT_NAME} INTERFACE HAS_LUA)
      target_link_libraries(${PROJECT_NAME} PUBLIC lua)
      list(APPEND TARGET_LIBS "lua")
      install(TARGETS lua EXPORT qboxTargets LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
      install(FILES ${lua_SOURCE_DIR}/lua.h ${lua_SOURCE_DIR}/luaconf.h
                    ${lua_SOURCE_DIR}/lualib.h ${lua_SOURCE_DIR}/lauxlib.h
              DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  endif()
endmacro()
################################################################################


# by default switch on verbosity

macro(gs_enable_testing)
  if (NOT GS_ONLY)
    if(BUILD_TESTING AND ("${PROJECT_NAME}" STREQUAL "${CMAKE_PROJECT_NAME}"))
      if (googletest_ADDED)
        # Force gtest DLLs to CMAKE_BINARY_DIR instead of CMAKE_BINARY_DIR/bin
        if(WIN32)
          set_target_properties(gtest gtest_main gmock gmock_main
            PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
          )
        endif()
      endif()
      enable_testing()
      cmake_host_system_information(RESULT _nproc QUERY NUMBER_OF_LOGICAL_CORES)
      math(EXPR _test_jobs "${_nproc} * 3 / 4")

      # On Windows, parallel test execution can lead to issues with duplicated shared libraries, so we limit it to 1.
      if(_test_jobs LESS 1)
          set(_test_jobs 1)
      endif()

      if (${PROJECT_NAME} STREQUAL "qbox")
          list(APPEND CMAKE_CTEST_ARGUMENTS "-j" "${_test_jobs}" "--output-on-failure")
      else()
          list(APPEND CMAKE_CTEST_ARGUMENTS "--repeat" "until-pass:3" "--output-on-failure")
      endif()
      add_subdirectory(tests)
    endif()
  endif()
endmacro()
