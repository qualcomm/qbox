# Building QBox

This page is the build guide hub. Choose an operating system guide for
prerequisites and complete configure, build, test, and install commands:

- [Linux](building/linux.md)
- [Windows](building/windows.md)
- [macOS](building/macos.md)

## Build requirements and support status

The root `CMakeLists.txt` declares CMake 3.14 as its minimum. However,
`CMakePresets.json` uses preset schema version 3, which requires CMake 3.21 or
newer. The root minimum does not prove that presets work with every CMake
version. Use CMake 3.21 or newer for the documented preset commands.

The build requires host Threads, pkg-config, `libelf`, `libzip`, GLib, and
`libfdt`. Python interpreter and development files are required when the
default `ENABLE_PYTHON_BINDER=ON` option is enabled. The OS guides describe the
dependency script. CMake fetches source dependencies, including SystemC, CCI,
and other CPM packages; `package-lock.cmake` pins packages. These fetched
packages do not replace required host development libraries. There is no
`.gitmodules` file describing build dependencies.

| Platform/version | Installer/configuration status | Qualification |
|---|---|---|
| Ubuntu 22.04 and 24.04 | In the Linux CI matrix | CI workflow configuration, not proof of a successful run |
| Ubuntu 20.04 | Accepted by the dependency script | Not in the Linux CI matrix; build/test status is unverified |
| Windows 11 ARM64, MSYS2 CLANGARM64 | In the Windows CI workflow with preset `clang` | Configured CI variant; not proof of a successful run |
| Windows x86_64, MSYS2 UCRT64 | Packages and `win-gcc*` presets are defined | UCRT64 workflow variant is commented out as unstable; no CI build is configured |
| macOS 15 and 26 | Installer and macOS CI matrix support these versions | CI workflow configuration, not proof of a successful run |
| macOS 14 | Not accepted by the current dependency script | Do not treat older README text as current installer support |

Ubuntu CI configures `gcc`, `gcc-debug`, `clang`, and `clang-debug` on both
Ubuntu versions. It configures `clang-lto` only on Ubuntu 24.04. macOS CI
configures `mac` and `mac-debug` on macOS 15 and 26. Windows CI configures the
generic `clang` preset only in CLANGARM64. Other `win-gcc*` and `win-clang*`
presets exist in CMake, but are not current Windows CI configurations.

## Build layout, tests, and options

Each preset writes to the repository's `build/` directory. The main runtime
executable is `build/platforms-vp`; test executables are under `build/tests/`.
To list tests in a configured build or select them by CTest regular expression,
use:

```bash
ctest --test-dir build -N
ctest --preset <preset> -R '<regular-expression>'
```

Presets default `LIBQEMU_TARGETS` to `aarch64;hexagon;xtensa;riscv64;riscv32`.
The CI commands narrow that to `aarch64;hexagon`. `ENABLE_PYTHON_BINDER` is ON
by default; use `-DENABLE_PYTHON_BINDER=OFF` to disable it. `clang-lto` enables
LTO and lld. Linux and macOS enable virgl/vircl renderer options by default;
Windows disables them by default.

The presets set the install prefix to `build/install`. Install with
`cmake --install build` to use that configured prefix. Do not pass `--prefix`
at install time: the root CMake configuration installs `qemu-components` and
`systemc-components` to the absolute `CMAKE_INSTALL_PREFIX` set at configure
time. An install-time override can therefore put those directories in the
configured prefix while placing other files under the override.

To use a custom prefix, set it during configuration, then use that same preset
for the build and install. For example:

```bash
cmake --preset gcc -DCMAKE_INSTALL_PREFIX="$HOME/.local/qbox"
cmake --build --preset gcc --parallel
cmake --install build
```

The install tree is not a self-contained bundle. Downstream projects must also
resolve required dependencies, including SystemC, CCI, fmt, reporting, and
libqemu.

For the experimental DragonWing platform, see
[its platform guide](../platforms/dragonwing/README.md). The separate
[hello-qbox example](../platforms/hello-qbox/README.md) documents a standalone
consumer project.
