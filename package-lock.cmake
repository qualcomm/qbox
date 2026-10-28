# CPM Package Lock
# This file should be committed to version control

# SystemC
set(LOCK_SYSTEMC_GIT
  https://github.com/accellera-official/systemc.git
)
set(LOCK_SYSTEMC_TAG
  febde7a3e007ce3030cf574a4cb6fcc13d0ed820 # TODO: Replace with release > 3.0.2
)

CPMDeclarePackage(SystemCLanguage
  NAME SystemCLanguage
  GIT_TAG "${LOCK_SYSTEMC_TAG}"
  GIT_REPOSITORY "${LOCK_SYSTEMC_GIT}"
  GIT_SHALLOW OFF
  OPTIONS
      "ENABLE_SUSPEND_ALL"
      "ENABLE_PHASE_CALLBACKS"
)

# SCP
set(LOCK_SCP_GIT
  https://github.com/accellera-official/systemc-common-practices.git
)
set(LOCK_SCP_TAG
  0821c329a1df363503f5ea12309cc5fa2c064530
)

CPMDeclarePackage(SCP
  NAME SCP
  GIT_TAG "${LOCK_SCP_TAG}"
  GIT_REPOSITORY "${LOCK_SCP_GIT}"
  # https://cmake.org/cmake/help/latest/module/ExternalProject.html#git
  # If GIT_SHALLOW is enabled then GIT_TAG works only with branch names
  # and tags. A commit hash is not allowed.
  GIT_SHALLOW OFF
)

# libqemu
set(LOCK_LIBQEMU_GIT
  https://github.com/qualcomm/qemu.git
)
set(LOCK_LIBQEMU_TAG
  libqemu-v11.0-v0.17
)

CPMDeclarePackage(qemu
    NAME libqemu
    GIT_REPOSITORY "${LOCK_LIBQEMU_GIT}"
    GIT_TAG "${LOCK_LIBQEMU_TAG}"
    GIT_SUBMODULES CMakeLists.txt
    GIT_SHALLOW ON
)
