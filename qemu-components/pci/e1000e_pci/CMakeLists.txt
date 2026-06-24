gs_create_dymod(e1000e_pci)

target_include_directories(
    e1000e_pci PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/qemu-components/pci/qemu_gpex/include>
  )
