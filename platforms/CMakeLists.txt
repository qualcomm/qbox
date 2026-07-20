set(UBUNTU_ARCH "aarch64" CACHE STRING "select the target architecture for ubuntu image")
if(UBUNTU_ARCH STREQUAL "riscv64")
    set(LIBQEMU_TARGETS riscv64)
else()
    set(LIBQEMU_TARGETS aarch64)
endif()

add_executable(platforms-vp
    ${PROJECT_SOURCE_DIR}/platforms/src/main.cc
)
if(APPLE)
    target_link_options(platforms-vp PRIVATE "LINKER:-dead_strip_dylibs")
else()
    target_link_options(platforms-vp PRIVATE "LINKER:-as-needed")
endif()

target_link_libraries(platforms-vp
    qbox-common
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND EXISTS "/etc/lsb-release")
    file(STRINGS "/etc/lsb-release" LSB_RELEASE_CONTENT REGEX "DISTRIB_ID.*Ubuntu")
    if(LSB_RELEASE_CONTENT)
        message("The build is done from an Ubuntu machine.")
        set(UBUNTU_BUILD_COMMAND ${PROJECT_SOURCE_DIR}/platforms/ubuntu/fw/build_linux_dist_image.sh -s 4G -p xorg,pciutils -d ubuntu -r noble -s 5G -a ${UBUNTU_ARCH})
        if (EXISTS ${CMAKE_SOURCE_DIR}/configs/fw/8775/bsp/lnx/ubuntu/conf.lua)
                add_custom_target(ubuntu
                    COMMAND ${UBUNTU_BUILD_COMMAND}
                    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/platforms/ubuntu/fw
                    COMMAND ${CMAKE_BINARY_DIR}/vp -p 'hex_coproc_dir=""' -l ${CMAKE_SOURCE_DIR}/configs/fw/8775/bsp/lnx/ubuntu/conf.lua --images-dir ${PROJECT_SOURCE_DIR}/platforms/ubuntu/fw/
                    DEPENDS vp
                )
        else()
            add_custom_target(ubuntu
                COMMAND ${UBUNTU_BUILD_COMMAND}
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/platforms/ubuntu/fw
                COMMAND ${qbox_BINARY_DIR}/platforms/platforms-vp -l ${PROJECT_SOURCE_DIR}/platforms/ubuntu/conf_${UBUNTU_ARCH}.lua
                DEPENDS platforms-vp
            )
        endif()
    else()
        message("The build is not done from an Ubuntu machine.")
    endif()
else()
    message("The build is done from a non-Linux system.")
endif()

add_subdirectory(cortex-m55-remote)
