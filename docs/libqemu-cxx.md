# libqemu-cxx -- QEMU C++ Wrapper

Libqemu-cxx exposes QEMU through C++ objects for use by QBox
components. See the [build guide](building.md) for build setup.

## Library Loading

The dynamic library loader uses `dlopen` on Linux and macOS. If
it is asked to load a separate instance of a library that is
already loaded, it copies the library to a unique file under
`/tmp`, loads that copy, then removes the temporary file. The
loaded library remains mapped after the file is removed. See
[`dynlib_loader.cc`](../systemc-components/common/src/dynlib_loader.cc)
for this platform-specific behavior.

The temporary file is removed after a successful load. This
guide does not claim that library symbols are unavailable to a
debugger. The source does not define a `DEBUG_TMP_LIBRARIES`
option, so do not use that name as a build flag.
