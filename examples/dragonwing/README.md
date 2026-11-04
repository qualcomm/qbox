# DragonWing: SA8775P QLI 2.0 Linux + CDSP

Boots Qualcomm Linux (QLI 2.0) on the SA8775P platform model, with the two
CDSP (Hexagon) subsystems, using QBox.

The platform is described by `configs/fw/8775/bsp/lnx/qli_2.0/conf.lua`. The
SoC models it needs live in `systemc-components/` (`SA8775P`, `pll`, `pub`,
`csr`, `wdog`, `mpm_control`, `ipcc`, `qupv3_qupv3_se_wrapper_se0`,
`aoss_pmic_arb_pmic_arb_core`, `apss_hm_apss_shared_apss_intu`) and are built
when `ENABLE_DRAGONWING=ON`.

## 1. Prerequisites

Ubuntu 24.04, GCC 13, CMake >= 3.28, and:

```bash
sudo apt install build-essential cmake ninja-build git pkg-config python3 \
    python3-venv libzip-dev libglib2.0-dev libpixman-1-dev libasio-dev
```

(The list is approximate; CMake reports anything else that is missing.)

## 2. Clone

After the DragonWing changes are available on the staging repository's `main`
branch, clone that repository:

```bash
git clone https://github.com/qcom-eng-sim/qbox-staging.git
cd qbox-staging
```

You also need the QLI images directory (`exp_qli_qcow2`) from the QQVP
`firmware-images` repository. It must contain `Image`, `simple1.dtb`,
`rootfs.ext4`, `smem_init.bin` and `arm64_bootloader.lua`.

`--images-dir` supplies the external fallback directory for Lua `image_file()`
lookups. For each image, the helper checks the directory of the Lua configuration
first, then the directory supplied to `--images-dir`. It does not download images
or infer a default firmware path.

## 3. Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_DRAGONWING=ON -DENABLE_PYTHON_BINDER=OFF \
      -DLIBQEMU_TARGETS="aarch64;hexagon"
make -C build -j$(nproc)
```

`LIBQEMU_TARGETS` must include `hexagon`; otherwise the run stops with
"target `Hexagon` disabled at compile time". The build produces
`build/platforms-vp` and the model libraries next to it.

## 4. Launch

Run from the `build` directory. `-p` options must come before `-l`.

```bash
cd build
./platforms-vp -p platform.hexagon_semihosting=true \
    --images-dir <path-to>/firmware-images/exp_qli_qcow2 \
    -l ../examples/dragonwing/configs/fw/8775/bsp/lnx/qli_2.0/conf.lua \
    | tee run.log
```

The first boot takes a few minutes. The guest serial console is this
terminal. Wait for `qcs9100-ride-sx login:` and log in (typically `root`).

## 5. Check in the guest

CDSP firmware and remoteproc:

```bash
ls /lib/firmware/qcom/sa8775p
dmesg | grep -i -E "remoteproc|cdsp|fastrpc"
```

FastRPC (cdsp0 is `-d 3`, cdsp1 is `-d 4`; run from any directory):

```bash
fastrpc_test -d 3 -U 0 -t linux -a v68
fastrpc_test -d 3 -U 1 -t linux -a v68
fastrpc_test -d 3 -U 0 -t linux -a v75
fastrpc_test -d 3 -U 1 -t linux -a v75
fastrpc_test -d 4 -U 0 -t linux -a v68
fastrpc_test -d 4 -U 1 -t linux -a v68
fastrpc_test -d 4 -U 0 -t linux -a v75
fastrpc_test -d 4 -U 1 -t linux -a v75
```

QNN (needs `/mnt/qnn` with `input_list.txt` and `vgg_ctx.bin` in the guest):

```bash
qnn-net-run --backend libQnnHtp.so --input_list /mnt/qnn/input_list.txt \
    --retrieve_context /mnt/qnn/output/vgg_ctx.bin --output_dir /mnt/qnn/output
```
