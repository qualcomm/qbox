gs_create_dymod(qemu_pcie)

target_include_directories(
    qemu_pcie PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/qemu-components/pci/qemu_gpex/include>
  )
