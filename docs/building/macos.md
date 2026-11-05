# Build on macOS

[Building overview](../building.md) | [Project README](../../README.md)

## Prerequisites

The dependency script supports macOS 15 and 26. These are also the macOS
versions configured by CI. The script does not support macOS 14, even if older
project text lists it. A configured workflow is not proof of a successful run.

Install Homebrew first, then run the project dependency script from the
repository root:

```bash
scripts/install_dependencies.sh
```

The script uses Homebrew for packages including CMake, `dtc`, `libelf`,
`libzip`, LLVM/lld, and SDL2. It also runs `pip install --user pexpect numpy`
and installs the `quic/quic/virglrenderer` Homebrew formula. The build requires
Threads, pkg-config, `libelf`, `libzip`, GLib, and `libfdt`; the default Python
binder also requires Python interpreter/development files. CMake fetches
SystemC, CCI, and other CPM source packages. The package lock pins their
versions; fetched packages do not replace host development libraries.

## Configure, build, test, and install

Run these commands from the repository root. They match the macOS CI
configuration, with the preset set to `mac`:

```bash
scripts/install_dependencies.sh
cmake --preset mac -DLIBQEMU_TARGETS="aarch64;hexagon"
cmake --build --preset mac --parallel
ctest --preset mac
```

The `mac` preset uses Release mode. Use `mac-debug` for a Debug build. The
workflow defines both presets on macOS 15 and 26.

To install the built files to the prefix set by the preset, run:

```bash
cmake --install build
```

This install tree is not a self-contained bundle. A downstream project must
also be able to find required dependencies, including SystemC, CCI, fmt,
reporting, and libqemu.

## Tests and output

List configured tests, or run selected tests with CTest's regular-expression
filter:

```bash
ctest --test-dir build -N
ctest --preset mac -R '<regular-expression>'
```

The main runtime executable is written to `build/platforms-vp`. Test
executables are under `build/tests/`. See the
[experimental DragonWing guide](../../platforms/dragonwing/README.md) or the
[standalone hello-qbox example](../../platforms/hello-qbox/README.md) for those
separate platform projects.
