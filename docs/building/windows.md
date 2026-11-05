# Build on Windows

[Building overview](../building.md) | [Project README](../../README.md)

QBox uses MSYS2 and CMake presets for Windows builds. The current Windows CI
configuration uses a Windows 11 ARM runner, the MSYS2 CLANGARM64 environment,
and the `clang` preset. The commands below follow that configuration.

## Prepare Windows and MSYS2

### Enable Developer Mode

QBox creates symbolic links during compilation. Enable Developer Mode so that
MSYS2 can create them without elevated privileges. Follow Microsoft's
[Developer Mode instructions](https://learn.microsoft.com/en-us/windows/apps/get-started/enable-your-device-for-development).

### Optional: use a Dev Drive

A Dev Drive can improve build performance. See Microsoft's
[Dev Drive guide](https://learn.microsoft.com/en-us/windows/dev-drive/). If you
use one, install MSYS2 on that drive. For example, use `D:\msys64` when the
drive is `D:`.

### Install MSYS2

Install MSYS2 from [msys2.org](https://www.msys2.org/). Launch one of its
environments from the Start Menu. The CI environment is **MSYS2 CLANGARM64**
for Windows on ARM64. MSYS2 environments are isolated, so install packages in
the environment where you build. UCRT64 is available for x86_64; MINGW64 is
not recommended by MSYS2.

In the selected MSYS2 terminal, update its packages. Restart the terminal if
the update asks you to, then run the project dependency script from the
repository root:

```bash
pacman -Syu
scripts/install_dependencies.sh
```

The script installs the MSYS2 base tools and uses pacboy for packages such as
the toolchain, CMake, `dtc`, `libzip`, `libelf`, `libslirp`, lld/LLVM,
`python-pexpect`, and SDL2. It is the source of truth for the package list.

### Optional: add organization certificates

MSYS2 does not use the Windows certificate store. If downloads fail with an SSL
error such as `self signed certificate in certificate chain`, copy your
organization's PEM certificate into the MSYS2 trust store, then update the
certificate database:

```bash
cp /path/to/certificate.pem /etc/pki/ca-trust/source/anchors/
update-ca-trust
```

See the [MSYS2 FAQ](https://www.msys2.org/docs/faq/) for more information.

### Optional: add Windows Terminal profiles

For an MSYS2 install at `C:\msys64`, add the profiles you need to Windows
Terminal's `settings.json`:

```json
{
  "profiles": {
    "list": [
      {
        "commandline": "C:/msys64/msys2_shell.cmd -defterm -here -no-start -clangarm64",
        "guid": "{5c03ab6d-5335-40fd-8b73-c5aab12cf419}",
        "hidden": false,
        "icon": "C:/msys64/clangarm64.ico",
        "name": "CLANGARM64 / MSYS2",
        "startingDirectory": "C:/msys64/home/%USERNAME%"
      },
      {
        "commandline": "C:/msys64/msys2_shell.cmd -defterm -here -no-start -ucrt64",
        "guid": "{17da3cac-b318-431e-8a3e-7fcdefe6d114}",
        "hidden": false,
        "icon": "C:/msys64/ucrt64.ico",
        "name": "UCRT64 / MSYS2",
        "startingDirectory": "C:/msys64/home/%USERNAME%"
      }
    ]
  }
}
```

Adjust the paths if MSYS2 is installed elsewhere.

## Configure, build, test, and install

Run these commands in the CLANGARM64 terminal from the repository root:

```bash
scripts/install_dependencies.sh
cmake --preset clang -DLIBQEMU_TARGETS="aarch64;hexagon"
cmake --build --preset clang --parallel
ctest --preset clang --parallel 1
cmake --install build
```

The test command runs tests serially, as the Windows CI configuration does.
The `clang` preset uses the repository's Clang toolchain file. CMake fetches
SystemC, CCI, and other CPM source packages; the package lock pins their
versions. The build also needs host packages such as `libelf`, `libzip`, GLib,
`libfdt`, Threads, and pkg-config. The default Python binder requires Python
interpreter/development files.

## Other Windows configurations and limitations

`CMakePresets.json` also defines `win-gcc`, `win-gcc-debug`, `win-clang`, and
`win-clang-debug`. These presets are alternatives in the repository, not the
current Windows CI configuration. The workflow comments out the UCRT64 x86_64
GCC variant because of instability; it does not establish that these
alternatives are tested or supported. Windows CI currently configures only
CLANGARM64 with the generic `clang` preset.

The Windows CMake configuration does not add the `component_constructor`,
`macs`, or `legacy_char_backend_stdio` targets. It also does not add the FSS
test suite. Names such as `ufs`, `ufs_lu`, `vnc`, `pl031`, `pflash_cfi`,
`fw_cfg`, and `ramfb` appeared in an older list, but the current CMake
exclusions do not establish that these components cannot be built. Do not
infer support or lack of support for them from that old list.

The main runtime executable is written to `build/platforms-vp`; test
executables are under `build/tests/`. The install tree is not a self-contained
bundle. Downstream projects must also resolve dependencies such as SystemC,
CCI, fmt, reporting, and libqemu.

See the [experimental DragonWing guide](../../platforms/dragonwing/README.md)
or the [standalone hello-qbox example](../../platforms/hello-qbox/README.md)
for those separate platform projects.
