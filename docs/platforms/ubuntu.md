# Ubuntu Linux Platform

QBox has a virtual platform configuration for Ubuntu Linux guests on
AArch64 and RISC-V 64. This page describes image creation and manual
build and run steps. For general build requirements, see
[Building QBox](../building.md).

## Create the guest image

The image script is `platforms/ubuntu/fw/build_linux_dist_image.sh`.
It creates image files in `platforms/ubuntu/fw/Artifacts/`. The script
states that Ubuntu 22.04.5 LTS is the tested host. This is a host note,
not a complete QBox host support matrix. The script uses `sudo`, needs
network access to fetch its external build tools and source, and needs
enough free disk space for the build and image files. The checked-out
script selects mkosi v24.3; for RISC-V it also builds OpenSBI v1.5.1.
These steps have not been run as part of this documentation update.

From the repository root, run the image script from its directory:
Run one command for the guest architecture you want to use.

```bash
cd platforms/ubuntu/fw

# AArch64 guest image:
./build_linux_dist_image.sh -s 4G -p xorg,pciutils -a aarch64

# RISC-V 64 guest image:
./build_linux_dist_image.sh -s 4G -p xorg,pciutils -a riscv64
```

The script supports Ubuntu Jammy and Noble for both architectures, and
Fedora 39 and 40 for AArch64. Without `-d` and `-r`, it uses Ubuntu
Jammy. Use `-h` for the script options.

The generated configuration uses these files:

| File | Purpose |
|---|---|
| `Image.bin` | Uncompressed Linux kernel image |
| `ubuntu.dtb` | Device tree passed to the kernel |
| `image_ext4_initrd.img` | Initramfs |
| `image_ext4.img` | Guest root filesystem |
| `fw_jump.bin` | OpenSBI firmware required by the RISC-V configuration |

## Build QBox

Return to the repository root (for example, `cd ../../..` from
`platforms/ubuntu/fw`). Configure and build the common virtual platform
for the guest architecture:

```bash
# AArch64:
cmake -S . -B build -DLIBQEMU_TARGETS=aarch64
cmake --build build --parallel

# Or RISC-V 64:
cmake -S . -B build -DLIBQEMU_TARGETS=riscv64
cmake --build build --parallel
```

The root CMake build produces the shared `build/platforms-vp`
executable. `LIBQEMU_TARGETS` selects the QEMU target compiled into
QBox; it does not select a different virtual-platform executable.

## Run the guest

Run from the repository root. The Lua files use their own locations to
find the image files under `platforms/ubuntu/fw/Artifacts/`.

```bash
# AArch64:
./build/platforms-vp -l platforms/ubuntu/conf_aarch64.lua

# Or RISC-V 64:
./build/platforms-vp -l platforms/ubuntu/conf_riscv64.lua
```

The guest login credentials configured by the image script are `root`
for both the user name and password.

> **Note:** The root CMake files define an `ubuntu` convenience target,
> but its current run command refers to `build/platforms/platforms-vp`.
> The root build creates `build/platforms-vp`, so use the explicit build
> and run commands above. This page does not change the CMake target.

## Related guides

- [Documentation index](../README.md)
- [Building QBox](../building.md)
