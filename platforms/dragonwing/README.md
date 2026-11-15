# Qualcomm Dragonwing™ Virtual Platform

This experimental configuration describes the Qualcomm Dragonwing™ SA8775P
platform in QBox. The steps below use the Qualcomm® Linux® (QLI 2.1.1)
IQ-9075 EVK prebuilt image. The platform includes two Hexagon CDSP subsystems.
The platform configuration is located at
`platforms/dragonwing/configs/fw/8775/bsp/lnx/qli_2.0/conf.lua`.

## Prerequisites

Follow [Building QBox](../../docs/building.md) for the host prerequisites.

Define the QBox checkout location before preparing the downloaded image:

```bash
export QBOX=/absolute/path/to/qbox
export FIRMWARE_DIR="$QBOX/platforms/dragonwing/configs/fw/8775/bsp/lnx/qli_2.0"
```

## Download and prepare the QLI image

1. From your working directory (the directory containing the QBox checkout),
   download **IQ-9075 EVK → qcom-distro → qcom-multimedia-image**, QLI 2.1.1:

   ```bash
   curl -fL --retry 3 -o qli-2.1.1-qcom-multimedia-image.zip \
     https://artifacts.codelinaro.org/artifactory/qli-ci/flashable-binaries/meta-qcom/qcom-distro/iq-9075-evk/qli-2.1.1-qcom-multimedia-image.zip
   ```

2. Extract the archive in that same working directory:

   ```bash
   unzip qli-2.1.1-qcom-multimedia-image.zip
   export IMAGES_DIR="$(pwd)/images/iq-9075-evk/qcom-multimedia-image-iq-9075-evk"
   ```

   The extracted directory contains `vmlinux` and `rootfs.img`.

3. Convert the ELF kernel to the raw `Image` required by the VP bootloader.
   Use GNU `objcopy` with support for `elf64-little`:

   ```bash
   objcopy -I elf64-little -O binary \
     -R .note -R .note.gnu.build-id -R .comment -S \
     "$IMAGES_DIR/vmlinux" "$IMAGES_DIR/Image"
   ```

4. Create or update the kernel and rootfs symlinks expected by `conf.lua`:

   ```bash
   ln -sfn "$IMAGES_DIR/Image" "$FIRMWARE_DIR/Image"
   ln -sfn "$IMAGES_DIR/rootfs.img" "$FIRMWARE_DIR/rootfs.ext4"
   ```

Keep these path variables available for the build and launch steps.

## Build

```bash
cd "$QBOX"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_DRAGONWING=ON -DENABLE_PYTHON_BINDER=OFF \
    -DLIBQEMU_TARGETS="aarch64;hexagon"
cmake --build build --parallel
```

## Run

```bash
cd "$QBOX"
./build/platforms-vp \
    --images-dir "$FIRMWARE_DIR" \
    -l platforms/dragonwing/configs/fw/8775/bsp/lnx/qli_2.0/conf.lua
```

Use these credentials to log in to the booted guest:

```text
Username: root
Password: oelinux123
```

## Validate FastRPC

   ```bash
   fastrpc_test -d 3 -U 0 -t linux -a v68
   ```

   A successful run reports:

   ```text
   [PASS] libhap_example.so
   Sum = 499500
   Max value = 999
   [PASS] libcalculator.so
   [PASS] libmultithreading.so

   Total tests run:    3
   Passed:             3
   Failed:             0
   ```

## Related guides

- [QBox build instructions](../../docs/building.md)
- [Documentation index](../../docs/README.md)
