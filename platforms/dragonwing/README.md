# Qualcomm Dragonwing™ Virtual Platform

This experimental configuration describes the Qualcomm Dragonwing™ SA8775P
platform in QBox. It targets Qualcomm® Linux® (QLI 2.0) and includes two
Hexagon CDSP subsystems. The platform configuration is located at
`platforms/dragonwing/configs/fw/8775/bsp/lnx/qli_2.0/conf.lua`.

## Prerequisites

Follow the standard QBox prerequisites in
[Building QBox](../../docs/building.md). The requirements below are specific to
this platform. Ubuntu 24.04, GCC 13, and CMake 3.28 are an example host setup,
not a supported-host matrix or a tested requirement.

The platform requires a compatible, authorized QLI firmware set. Define the
QBox checkout location before preparing the files:

```bash
export QBOX=/absolute/path/to/qbox
export FIRMWARE_DIR="$QBOX/platforms/dragonwing/configs/fw/8775/bsp/lnx/qli_2.0"
```

The following seven files are required:

| File | Location |
| --- | --- |
| `Image` | `$FIRMWARE_DIR` (symlink created below) |
| `rootfs.ext4` | `$FIRMWARE_DIR` (symlink created below) |
| `sa8775-qli.dtb` | `$FIRMWARE_DIR` |
| `smem_init.bin` | `$FIRMWARE_DIR` |
| `arm64_bootloader.lua` | `$FIRMWARE_DIR` |
| `cmd_db.bin` | `$QBOX/platforms/dragonwing/configs/fw/common_conf/auto_gen4` |
| `cmd_db_header.bin` | `$QBOX/platforms/dragonwing/configs/fw/common_conf/auto_gen4` |

Follow the directions below to obtain these files.

> **Note:** No changes to the Lua configuration files are necessary.

## Prepare the QLI firmware (`Image` and `rootfs.ext4`)

Build `Image` and `rootfs.ext4` from the `wrynose` branch of the open-source
Qualcomm Linux `meta-qcom` repository. The version numbers and timestamps below
are examples and may differ in your build.

1. Create a working directory and fetch the sources:

   ```bash
   mkdir -p meta
   cd meta
   git clone --branch wrynose https://github.com/qualcomm-linux/meta-qcom.git
   ```

2. Clone the DSP binaries repository and check out the validated
   revision:

   ```bash
   git clone https://github.com/linux-msm/hexagon-dsp-binaries.git
   git -C hexagon-dsp-binaries checkout --detach 53eafda6d174ad7a7a965215cd0b224e855620de
   ```
3. Stage the CDSP libraries and testsig:

   `   mkdir -p meta-qcom/recipes-bsp/hexagon-dsp-binaries/files/qbox-cdsp
   `
   ```
   cp -L \
      hexagon-dsp-binaries/sa8775p/Qualcomm/SA8775P-RIDE/cdsp-DSP.AT.1.0.1-00204-LEMANS-1/* \
      meta-qcom/recipes-bsp/hexagon-dsp-binaries/files/qbox-cdsp/

   cp $QBOX/platforms/dragonwing/testsig-0x12345678.so \
      meta-qcom/recipes-bsp/hexagon-dsp-binaries/files/qbox-cdsp/
   ```

4. Append the QBox VP board mapping to 

   ```
   cat > meta-qcom/recipes-bsp/hexagon-dsp-binaries/hexagon-dsp-binaries_%.bbappend <<'EOF'
   FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

   SRC_URI += "file://qbox-cdsp/"

   do_install:append() {
      # Map the VP board name to the installed SA8775P DSP files.
      printf '\n  Qualcomm QCS9100 Ride:\n    DSP_LIBRARY_PATH: sa8775p/Qualcomm/SA8775P-RIDE/dsp\n' \
         >> "${D}${datadir}/qcom/conf.d/hexagon-dsp-binaries-qualcomm-sa8775p-ride.yaml"

      # Install the CDSP libraries and testsig.
      dsp_dir="${D}${datadir}/qcom/sa8775p/Qualcomm/SA8775P-RIDE/dsp/cdsp"
      install -d "$dsp_dir"
      cp -R --no-preserve=ownership --remove-destination \
         "${UNPACKDIR}/qbox-cdsp/." "$dsp_dir/"
   }
   EOF
   ```

5. Open a `kas` build shell:

   ```bash
   kas shell meta-qcom/ci/qcs9100-ride-sx.yml:meta-qcom/ci/qcom-distro.yml:meta-qcom/ci/base.lock.yml
   ```

6. In the `kas` shell, build the Linux kernel and root filesystem:

   ```bash
   umask 022
   bitbake qcom-multimedia-proprietary-image
   ```

   The generated files are under paths similar to these:

   ```text
   /build/tmp/deploy/images/qcs9100-ride-sx/qcom-multimedia-proprietary-image-qcs9100-ride-sx.rootfs.ext4
   /build/tmp/deploy/images/qcs9100-ride-sx/Image
   ```

7. Outside the `kas` shell, create the required firmware symlinks. Replace the
   example filenames with the exact files produced by your build:

   ```bash
   export QBOX=/absolute/path/to/qbox
   export QLI_WORKDIR=/absolute/path/to/meta
   export FIRMWARE_DIR="$QBOX/platforms/dragonwing/configs/fw/8775/bsp/lnx/qli_2.0"

   export KERNEL_IMAGE="$QLI_WORKDIR/build/tmp/deploy/images/qcs9100-ride-sx/Image"
   export ROOTFS_IMAGE="$QLI_WORKDIR/build/tmp/deploy/images/qcs9100-ride-sx/qcom-multimedia-proprietary-image-qcs9100-ride-sx.rootfs.ext4"

   ln -s "$KERNEL_IMAGE" "$FIRMWARE_DIR/Image"
   ln -s "$ROOTFS_IMAGE" "$FIRMWARE_DIR/rootfs.ext4"
   ```

Alternatively, copy the generated files to `Image` and `rootfs.ext4` in the
directory supplied through `--images-dir`.

## Build

Use a development checkout of the
[Qualcomm QBox project](https://github.com/qualcomm/qbox). Do not assume that a
public checkout includes this configuration; confirm that the Dragonwing files
listed above exist before building. QBox uses the shared `platforms-vp`
executable; there is no separate Dragonwing executable.

From the repository root, run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_DRAGONWING=ON -DENABLE_PYTHON_BINDER=OFF \
    -DLIBQEMU_TARGETS="aarch64;hexagon"
cmake --build build --parallel
```

These options enable the Dragonwing model and build the AArch64 and Hexagon
QEMU targets. This configuration requires the Hexagon target. The build command
does not fetch or install firmware assets.

## Run

From the repository root, pass the external firmware directory through
`--images-dir`. The Lua configuration checks its own directory first and then
uses this fallback directory:

```bash
./build/platforms-vp \
    --images-dir "$QBOX/platforms/dragonwing/configs/fw/8775/bsp/lnx/qli_2.0" \
    -l platforms/dragonwing/configs/fw/8775/bsp/lnx/qli_2.0/conf.lua
```

Use these credentials to log in to the booted QQVP guest:

```text
Username: root
Password: oelinux123
```

## Validate FastRPC:

   ```bash
   fastrpc_test -d 3 -U 0 -t linux -a v68
   ```

   Expected output:

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
 