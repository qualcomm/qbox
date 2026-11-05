# Build on Linux

[Building overview](../building.md) | [Project README](../../README.md)

## Prerequisites

Use Ubuntu 22.04 or 24.04 for the Linux versions listed in the CI configuration.
The dependency script also accepts Ubuntu 20.04, but CI does not configure that
version. This is not evidence that all builds and tests pass on 20.04.

Install the host packages with the project script:

```bash
sudo scripts/install_dependencies.sh
```

The script installs the packages used by the project build, including CMake,
compilers, `libelf`, `libzip`, GLib, `libfdt`, and LLVM tools. The build also
needs Threads, pkg-config, and Python interpreter/development files when the
default Python binder is enabled. CMake fetches source dependencies such as
SystemC, CCI, and other CPM packages; the package lock pins package versions.
These fetched packages do not remove the need for host development libraries.

## Configure, build, test, and install

Run these commands from the repository root. This is the Ubuntu CI quickstart:

```bash
sudo scripts/install_dependencies.sh
cmake --preset gcc -DLIBQEMU_TARGETS="aarch64;hexagon"
cmake --build --preset gcc --parallel
ctest --preset gcc
```

The `gcc` preset uses Release mode. Other presets include `gcc-debug`,
`clang`, `clang-debug`, and `clang-lto`. The Ubuntu CI matrix configures
`gcc`, `gcc-debug`, `clang`, and `clang-debug` on Ubuntu 22.04 and 24.04. It
configures `clang-lto` on Ubuntu 24.04 only.

To install the built files to the prefix set by the preset, run:

```bash
cmake --install build
```

This install tree is not a self-contained bundle. A downstream project must
also be able to find required dependencies, including SystemC, CCI, fmt,
reporting, and libqemu.

## Tests and output

List the tests configured in the build tree, then select tests with CTest's
regular-expression filter:

```bash
ctest --test-dir build -N
ctest --preset gcc -R '<regular-expression>'
```

Do not assume every test has passed on a given host just because a preset or CI
job is defined. The documented CI matrix describes configured jobs; it is not a
report of a successful run.

The main runtime executable is written to `build/platforms-vp`. The test
executables are under `build/tests/`.

## Options and platform notes

The presets default `LIBQEMU_TARGETS` to `aarch64;hexagon;xtensa;riscv64;riscv32`.
The CI command above narrows this list. `ENABLE_PYTHON_BINDER` is ON by default;
set `-DENABLE_PYTHON_BINDER=OFF` to disable that optional component. The
`clang-lto` preset enables LTO and lld.

For the experimental DragonWing platform, see the
[DragonWing guide](../../platforms/dragonwing/README.md). For the separate
hello-qbox consumer example, see [hello-qbox](../../platforms/hello-qbox/README.md).
