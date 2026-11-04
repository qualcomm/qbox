add_subdirectory(addrtr)
add_subdirectory(backends)
add_subdirectory(exclusive_monitor)
add_subdirectory(keep_alive)
add_subdirectory(loader)
add_subdirectory(gs_memory)
add_subdirectory(memory_dumper)
add_subdirectory(pass)
if(ENABLE_PYTHON_BINDER AND (NOT GS_ONLY))
    add_subdirectory(python_binder)
endif()
add_subdirectory(router)
add_subdirectory(reg_router)
add_subdirectory(timeprinter)
add_subdirectory(tlm_bus_width_bridges)
add_subdirectory(uart)
add_subdirectory(write_gpio)
add_subdirectory(realtimelimiter)
add_subdirectory(dmi_converter)
add_subdirectory(monitor)
add_subdirectory(biflow_router)
add_subdirectory(exiter)
add_subdirectory(generic_lua_model)
add_subdirectory(container_builder)
add_subdirectory(smmu500)
add_subdirectory(smmuv3)
if(NOT WIN32)
    add_subdirectory(macs)
    add_subdirectory(component_constructor)
endif()


# SA8775P QLI 2.0 + CDSP (Hexagon) platform models, used by
# examples/dragonwing.
option(ENABLE_DRAGONWING "Build the DragonWing (SA8775P QLI + CDSP) models" OFF)
if(ENABLE_DRAGONWING)
    set(DRAGONWING_COMPONENTS
        SA8775P
        pll
        pub
        csr
        wdog
        mpm_control
        ipcc
        qupv3_qupv3_se_wrapper_se0
        aoss_pmic_arb_pmic_arb_core
        apss_hm_apss_shared_apss_intu
    )
    # The components #include <zip.h>; qbox only links libzip privately, so
    # add it to every target created by a component directory.
    foreach(_comp IN LISTS DRAGONWING_COMPONENTS)
        add_subdirectory(${_comp})
        get_property(_comp_targets DIRECTORY ${_comp} PROPERTY BUILDSYSTEM_TARGETS)
        foreach(_target IN LISTS _comp_targets)
            target_link_libraries(${_target} PRIVATE PkgConfig::LIBZIP)
        endforeach()
    endforeach()
endif()
