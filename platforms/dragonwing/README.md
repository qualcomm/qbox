# Qualcomm Dragonwing™ Virtual Platform

This experimental configuration describes the Qualcomm Dragonwing™
SA8775P platform in QBox. It targets Qualcomm® Linux® (QLI 2.0) and
includes two Hexagon CDSP subsystems. The configuration is under
`platforms/dragonwing/configs/fw/8775/bsp/lnx/qli_2.0/conf.lua`.

> **Status:** Build and boot are not verified here. The example commands
> show the source configuration and build options; they do not confirm
> that a particular checkout, firmware set, or QNN workload will work.

## Prerequisites

Use the standard QBox build prerequisites in
[Building QBox](../../docs/building.md). The options below are specific
to this platform. Ubuntu 24.04, GCC 13, and CMake 3.28 are an example
host setup, not a supported-host matrix or a tested requirement.

The platform needs a compatible, authorized external QLI firmware set.
The configuration looks for all five of these files:

- `Image`
- `simple1.dtb`
- `rootfs.ext4`
- `smem_init.bin`
- `arm64_bootloader.lua`

Obtain a set through an approved source and confirm that it matches the
platform configuration. No firmware download URL or access method is
provided here. The guest QNN libraries, models, input files, and runtime
context are also not bundled. QNN workloads require a compatible guest
setup and target-specific assets.

## Build

Use a development checkout of the
[Qualcomm QBox project](https://github.com/qualcomm/qbox). Do not assume
that a public checkout includes this configuration: confirm that the
Dragonwing files listed above exist in your checkout before building.
The repository is built with QBox's shared `platforms-vp` executable;
there is no separate Dragonwing executable.

From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_DRAGONWING=ON -DENABLE_PYTHON_BINDER=OFF \
    -DLIBQEMU_TARGETS="aarch64;hexagon"
cmake --build build --parallel
```

These options enable the Dragonwing model and compile the AArch64 and
Hexagon QEMU targets. The Hexagon target is needed by this configuration.
The command does not fetch or install firmware assets.

## Run

From the repository root, pass the external firmware directory to
`--images-dir`. The Lua configuration checks its own directory first,
then this fallback directory. For example:

```bash
./build/platforms-vp --images-dir <path-to-compatible-firmware-set> \
    -l platforms/dragonwing/configs/fw/8775/bsp/lnx/qli_2.0/conf.lua
```

The files in the firmware directory must include the five names listed
above. A successful build does not establish that the firmware will boot
or that a QNN workload will run.

## Related guides

- [QBox build instructions](../../docs/building.md)
- [Documentation index](../../docs/README.md)
