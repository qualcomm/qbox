gs_create_dymod(qemu_rciep_pci)

target_include_directories(
    qemu_rciep_pci PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/qemu-components/pci/qemu_gpex/include>
  )
