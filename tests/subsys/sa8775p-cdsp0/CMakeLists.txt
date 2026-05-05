# SA8775P CDSP0 subsystem configuration test
# Verifies device placement and IRQ wiring using baremetal hexagon assembly

# Firmware is assembled/linked/objcopied by the shared helper defined in
# tests/qbox/CMakeLists.txt (which also locates the LLVM tools). It produces
# ${CMAKE_CURRENT_BINARY_DIR}/sa8775p-cdsp0-fw.bin and the custom target
# sa8775p-cdsp0-fw-firmware.
qbox_build_firmware_bin(sa8775p-cdsp0-fw hexagon ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(sa8775p-cdsp0-test sa8775p-cdsp0.cc)
qbox_attach_firmware(sa8775p-cdsp0-test sa8775p-cdsp0-fw)

target_include_directories(sa8775p-cdsp0-test PRIVATE
    ${PROJECT_SOURCE_DIR}/tests/qbox/include
    ${PROJECT_SOURCE_DIR}/tests/qbox/cpu/hexagon
)

target_link_libraries(sa8775p-cdsp0-test PRIVATE
    qbox-common
    qemu_cpu_hexagon
    hexagon_globalreg
    hexagon_l2vic
    hexagon_tlb
    qemu_hexagon_qtimer
    qemu_pl011
    qcom_ipcc
    qcom_tcsr
    qcom_dspss_pub
    qcom_wdog
    qcom_turing_rsc
    qcom_turing_gdscr
    qcom_cdsp0_clkctl
    qcom_hwkm_prng
    qcom_turing_pll
    qcom_qdsp6ss_clkctl
    gs_memory
    router
)

add_test(NAME sa8775p-cdsp0-test COMMAND sa8775p-cdsp0-test)
set_tests_properties(sa8775p-cdsp0-test PROPERTIES TIMEOUT 30)
