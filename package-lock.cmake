# CPM Package Lock
# This file should be committed to version control

# SystemC
CPMDeclarePackage(SystemCLanguage
  NAME SystemCLanguage
  GIT_TAG febde7a3e007ce3030cf574a4cb6fcc13d0ed820 # TODO: Replace with release > 3.0.2
  GIT_REPOSITORY ${SYSTEMC_GIT}
  GIT_SHALLOW OFF
  OPTIONS
      "ENABLE_SUSPEND_ALL"
      "ENABLE_PHASE_CALLBACKS"
)

# SCP
CPMDeclarePackage(SCP
  NAME SCP
  GIT_TAG 0821c329a1df363503f5ea12309cc5fa2c064530
  GIT_REPOSITORY ${SCP_GIT}
  # https://cmake.org/cmake/help/latest/module/ExternalProject.html#git
  # If GIT_SHALLOW is enabled then GIT_TAG works only with branch names
  # and tags. A commit hash is not allowed.
  GIT_SHALLOW OFF
)

# qemu (unversioned)
CPMDeclarePackage(qemu
    NAME libqemu
    GIT_REPOSITORY ${LIBQEMU_GIT}
    GIT_TAG libqemu-v11.0-v0.5
    GIT_SUBMODULES CMakeLists.txt
    GIT_SHALLOW ON
)
