# libqbox -- QEMU/SystemC Integration

Libqbox encapsulates QEMU in SystemC such that it can be
instantiated as a SystemC TLM-2.0 model.

## QEMU Instantiation

A `QemuInstanceManager` is required to create a QEMU instance.
The manager holds and maintains instances until the end of
execution. A `QemuInstance` can contain one or many CPUs and
other devices.

### C++ API

```cpp
QemuInstanceManager m_inst_mgr;
QemuInstance m_qemu_inst("qemu_inst", &m_inst_mgr, QemuInstance::Target::AARCH64);
```

Add CPUs:

```cpp
sc_core::sc_vector<cpu_arm_cortexA53> m_cpus;
m_cpus("cpu", 32, [this](const char *n, size_t i) {
    return new cpu_arm_cortexA53(n, m_qemu_inst);
});
```

Add interrupt controllers and devices:

```cpp
arm_gicv3 m_gic("gic", m_qemu_inst);
qemu_pl011 m_uart("uart", m_qemu_inst);
```

### Lua Configuration

```lua
qemu_inst_mgr = {
    moduletype = "QemuInstanceManager"
}
```

Add CPUs via Lua:

```lua
if (ARM_NUM_CPUS > 0) then
    for i = 0, (ARM_NUM_CPUS - 1) do
        local cpu = {
            moduletype = "cpu_arm_cortexA53",
            args = {"&platform.qemu_inst"},
            mem = {bind = "&router.target_socket"},
            has_el2 = true,
            has_el3 = false,
            irq_timer_phys_out = {bind = "&gic_0.ppi_in_cpu_" .. i .. "_" .. ARCH_TIMER_NS_EL1_IRQ},
            irq_timer_virt_out = {bind = "&gic_0.ppi_in_cpu_" .. i .. "_" .. ARCH_TIMER_VIRT_IRQ},
            irq_timer_hyp_out = {bind = "&gic_0.ppi_in_cpu_" .. i .. "_" .. ARCH_TIMER_NS_EL2_IRQ},
            irq_timer_sec_out = {bind = "&gic_0.ppi_in_cpu_" .. i .. "_" .. ARCH_TIMER_S_EL1_IRQ},
            psci_conduit = "hvc",
            mp_affinity = (math.floor(i / 8) << 8) | (i % 8),
            start_powered_off = true,
            cntfrq_hz = 6250000,
        }
        if (i == 0) then
            cpu["rvbar"] = 0x00000000
            cpu["start_powered_off"] = false
        end
        platform["cpu_" .. tostring(i)] = cpu
    end
end
```

Configure an interrupt controller and UART:

```lua
gic_0 = {
    moduletype = "arm_gicv3",
    args = {"&platform.qemu_inst"},
    dist_iface    = {address = 0xc8000000, size = 0x10000, bind = "&router.initiator_socket"},
    redist_iface_0 = {address = 0xc8010000, size = 0x20000, bind = "&router.initiator_socket"},
    num_cpus = ARM_NUM_CPUS,
    redist_region = {ARM_NUM_CPUS / NUM_REDISTS},
    num_spi = 64,
}

qemu_pl011 = {
    moduletype = "qemu_pl011",
    args = {"&platform.qemu_inst"},
    mem = {address = 0xc0000000, size = 0x1000, bind = "&router.initiator_socket"},
    irq_out = {bind = "&gic_0.spi_in_1"},
}
```

## QEMU Arguments

The QEMU instance provides these default arguments:

```
-M none          (no machine)
-m 2048          (internal buffer sizes)
-monitor null    (no monitor)
-serial null     (no serial backend)
```

If no display argument has been set by a component,
`-display none` is also added.

Additional arguments can be appended via the `qemu_args` CCI
parameter on the instance:

```lua
["platform.qemu_inst.qemu_args"] = "-D file.log -trace pattern1 -trace pattern2"
```

To set a specific QEMU option, use the `qemu_args.` prefix:

```lua
["platform.qemu_inst.qemu_args.-monitor"] = "tcp:127.0.0.1:55555,server,nowait"
```

The latter form cannot set the same option multiple times,
since a parameter definition overrides any previous one.

### Telnet Monitor Example

```lua
["platform.qemu_inst.qemu_args.-monitor"] = "tcp:127.0.0.1:55555,server,nowait"
```

Connect with:

```bash
telnet 127.0.0.1 55555
```

Do **not** use QEMU arguments to enable GDB -- use the GDB
port parameter instead.

## GDB Support

To attach GDB, set the `gdb_port` CCI parameter on the **QEMU instance** to a
non-zero value:

```bash
./build/platforms/platforms-vp --gs_luafile conf.lua -p platform.qemu_inst.gdb_port=1234
```

This opens a GDB server on port 1234 for that instance. Every CPU of the instance
appears as a GDB thread (`info threads` in GDB), so one port debugs all of them.
The virtual platform will wait for GDB to connect before proceeding.

`gdb_port` belongs to the instance rather than to a CPU because QEMU's GDB stub
state is global to an instance -- there can only be one stub per instance.
Setting it on a CPU still works but is **deprecated**: the value is forwarded to
the owning instance and a warning is logged. Setting it on two CPUs of the same
instance used to abort the process (`gdbstub: couldn't create chardev`); it now
warns and keeps the first value.

## Supported Components

### CPUs

**ARM:**
- Cortex-A53, Cortex-A55, Cortex-A76, Cortex-A710
- Cortex-M7, Cortex-M55
- Cortex-R5, Cortex-R52
- Neoverse-N1, Neoverse-N2

**RISC-V:**
- riscv32, riscv64
- SiFive X280

**Hexagon:**
- Hexagon DSP

#### CPU CCI Parameters

The following parameters are shared by most ARM A-profile CPUs
(from `cortex-a53.h`):

| Name | Type | Default | Description |
|------|------|---------|-------------|
| mp_affinity | unsigned int | 0 | Multi-processor affinity value |
| has_el2 | bool | true | ARM virtualization extensions |
| has_el3 | bool | true | ARM secure-mode extensions |
| start_powered_off | bool | false | Start CPU in powered-off state |
| psci_conduit | string | "disabled" | PSCI conduit: "disabled", "hvc", or "smc" |
| rvbar | uint64_t | 0 | Reset vector base address register |
| cntfrq_hz | uint64_t | 0 | Generic Timer CNTFRQ in Hz |
| gdb_port | (from base) | 0 | **Deprecated** -- set `gdb_port` on the QEMU instance instead. Forwarded there with a warning. |

Note: Cortex-M and Cortex-R CPUs have different parameter sets.

### Interrupt Controllers

- **ARM GICv2** -- Generic Interrupt Controller v2
- **ARM GICv3** -- Generic Interrupt Controller v3
  (with optional ITS)
- **ARMv7-M NVIC** -- Nested Vectored Interrupt Controller
- **SiFive PLIC** -- Platform-Level Interrupt Controller
  (RISC-V)
- **RISC-V ACLINT SWI** -- Software interrupt controller
- **Hexagon L2VIC** -- Hexagon L2 vectored interrupt
  controller

#### Interrupt Controller CCI Parameters

**GICv3** (`arm_gicv3`):

| Name | Type | Default | Description |
|------|------|---------|-------------|
| num_cpus | unsigned int | 0 | Number of CPU interfaces |
| num_spi | unsigned int | 0 | Number of shared peripheral interrupts |
| revision | unsigned int | 3 | GIC revision (3=v3, 4=v4) |
| redist_region | vector<uint> | [] | Redistributor regions configuration |
| has_security_extensions | bool | false | Enable security extensions |
| has_lpi | bool | false | Enable Locality-specific Peripheral Interrupts |

**GICv2** (`arm_gicv2`):

| Name | Type | Default | Description |
|------|------|---------|-------------|
| num_cpu | unsigned int | 0 | Number of CPU interfaces |
| num_spi | unsigned int | 0 | Number of shared peripheral interrupts |
| revision | unsigned int | 2 | GIC revision (1=v1, 2=v2, 0=11MPCore) |
| has_virt_extensions | bool | false | Virtualization extensions (v2 only) |
| has_security_extensions | bool | false | Security extensions |
| num_prio_bits | unsigned int | 8 | Priority bits |
| has_msi_support | bool | false | Enable GICv2m MSI support |

### UARTs

- **PL011** -- ARM UART
- **16550** -- General-purpose UART
- **SiFive UART** -- RISC-V UART

### VNC

QBox provides a VNC interface for GPU display outputs.

Single display:

```lua
gpu = {
    moduletype = "virtio_gpu_gl_pci",
    args = ...,
}
platform["gpu_0"] = gpu

vnc = {
    moduletype = "vnc",
    gpu_output = 0,
    args = {"&platform.gpu_0"},
}
platform["vnc_0"] = vnc
```

Multi-display (set `outputs` on the GPU and assign a VNC
session to each):

```lua
gpu = {
    moduletype = "virtio_gpu_gl_pci",
    outputs = 2,
    args = ...,
}
platform["gpu_0"] = gpu

vnc0 = {
    moduletype = "vnc",
    gpu_output = 0,
    args = {"&platform.gpu_0"},
}
vnc1 = {
    moduletype = "vnc",
    gpu_output = 1,
    args = {"&platform.gpu_0"},
}
platform["vnc_0"] = vnc0
platform["vnc_1"] = vnc1
```

The optional `params` field is passed to QEMU for additional
VNC configuration (see QEMU VNC documentation). The fields
`:`, `head=`, and `display=` are generated by the VNC module
and are not forwarded if passed via `params`.

Limitations:
- On macOS, VNC is restricted to `virtio-gpu-pci` GPUs.

### Other Devices

The library also provides TLM socket initiators and targets
for QEMU, as well as additional device models including
timers, PCI devices (virtio-gpu, virtio-net, ivshmem, etc.),
virtio MMIO devices, USB controllers, and more. See the
`qemu-components/` source directory for the full list.

### PCI Devices

All PCI devices attach to a GPEX (Generic PCI Express) host
bridge via `add_device()`. The `qemu_gpex` host bridge
exposes ECAM, MMIO, MMIO-high, and PIO target sockets, plus
a `bus_master` initiator socket and four `irq_out` signals.

Available PCI devices:

- **virtio_gpu_pci** / **virtio_gpu_gl_pci** /
  **virtio_gpu_cl_pci** / **virtio_gpu_qnn_pci** -- GPU
  variants (software rendering, OpenGL, OpenCL, QNN
  respectively).
- **virtio_net_pci** -- Virtio network adapter.
  CCI: `mac` (string), `netdev_str` (string, default
  `"type=user"`).
- **virtio_sound_pci** -- Virtio sound device (default ALSA
  audio driver).
- **qemu_xhci** -- USB 3.0 eXtensible Host Controller.
  Provides `add_device()` for attaching USB devices.
- **ivshmem_plain** -- Inter-VM shared memory.
  CCI: `shm_path` (string), `shm_size` (uint32_t, default
  1024 MB).
- **rtl8139_pci** -- Realtek 8139 NIC.
  CCI: `mac` (string), `netdev_str` (string, default
  `"type=user"`).
- **vhost_user_vsock_pci** -- Vhost-user virtual socket
  transport.

### Virtio MMIO Devices

Memory-mapped virtio devices. Each inherits from
`QemuVirtioMMIO`, which provides a `socket` (target) for
MMIO register access and an `irq_out` signal.

- **virtio_mmio_blk** -- Block device.
  CCI: `blkdev_str` (string).
- **virtio_mmio_net** -- Network device.
  CCI: `netdev_str` (string, default
  `"user,hostfwd=tcp::2222-:22"`).
- **virtio_mmio_gpugl** -- GPU with OpenGL support.
- **virtio_mmio_sound** -- Sound device (default ALSA audio
  driver).

### USB Devices

USB devices attach to a `qemu_xhci` host controller via
`add_device()`:

- **usb_storage** -- USB mass storage.
  CCI: `blkdev_str` (string, required).
- **usb_host** -- USB host passthrough. Forwards a physical
  USB device into the guest.
- **qemu_kbd** -- USB keyboard.
- **qemu_tablet** -- USB tablet (absolute pointing device).

### Input Devices

- **virtio_keyboard_pci** -- Virtio PCI keyboard. Attaches
  to a GPEX host bridge.
- **virtio_mouse_pci** -- Virtio PCI mouse. Attaches to a
  GPEX host bridge.

### Firmware and System

- **fw_cfg** -- QEMU firmware configuration device.
  CCI: `data_width` (uint32_t, default 8), `num_cpus`
  (uint32_t).
  Sockets: `ctl_target_socket`, `data_target_socket`,
  `dma_target_socket`.
- **pl031** -- ARM PrimeCell Real Time Clock.
  Sockets: `q_socket` (target), `irq_out`.
- **ramfb** -- RAM framebuffer display. Requires a `fw_cfg`
  instance to be created first (passed as a constructor
  argument).
- **pflash_cfi** -- Parallel NOR flash (CFI-compliant).
  Supports both CFI type 01 and type 02. Extensive CCI
  parameters for flash geometry (`num_blocks`,
  `sector_length`), bus width (`width`, `device_width`,
  `max_device_width`), ID bytes (`id0`--`id3`), and unlock
  addresses.
  Socket: `socket` (target).
- **qmp** -- QEMU Machine Protocol / HMP monitor interface.
  CCI: `qmp_str` (string, QEMU chardev option),
  `monitor` (bool, default true for HMP mode).
  Socket: `qmp_socket` (biflow).

### ARM SMMU

ARM MMU-500 System MMU (IOMMU).

CCI parameters:

| Name | Type | Default | Description |
|------|------|---------|-------------|
| pamax | uint32_t | 48 | Physical address size |
| num_smr | uint16_t | 48 | Number of stream match registers |
| num_cb | uint16_t | 16 | Number of context banks |
| num_pages | uint16_t | 16 | Number of register pages |
| version | uint8_t | 0x21 | SMMU version |
| num_tbu | uint8_t | 1 | Number of Translation Buffer Units |

Sockets: `register_socket` (target),
`upstream_socket` (vector of targets, sized by `num_tbu`),
`downstream_socket` (vector of initiators, sized by
`num_tbu`), `irq_context` (vector of initiator signals,
sized by `num_cb`), `irq_global` (initiator signal).

### Timers

- **riscv_aclint_mtimer** -- RISC-V ACLINT machine timer.

  | Name | Type | Default | Description |
  |------|------|---------|-------------|
  | hartid_base | uint32_t | 0 | Base hart ID |
  | num_harts | unsigned int | 0 | Number of harts connected |
  | timecmp_base | uint64_t | 0 | TIMECMP registers base address |
  | time_base | uint64_t | 0 | TIME registers base address |
  | aperture_size | uint64_t | 0 | CLINT address space size |
  | timebase_freq | uint32_t | 10000000 | Timebase frequency in Hz |
  | provide_rdtime | bool | false | Provide CPU with rdtime register |

  Sockets: `socket` (target), `timer_irq` (vector of
  initiator signals, sized by `num_harts`).

- **qemu_hexagon_qtimer** -- Hexagon QTimer.

  | Name | Type | Default | Description |
  |------|------|---------|-------------|
  | nr_frames | unsigned int | 2 | Number of timer frames |
  | nr_views | unsigned int | 1 | Number of views |
  | cnttid | unsigned int | 0x11 | Value of CNTTID register |

  Sockets: `socket` (target), `view_socket` (target),
  `irq` (vector of initiator signals, sized by
  `nr_frames`).

### Other

- **global_peripheral_initiator** -- Provides global memory
  space access for QEMU SysBus devices that need a
  system-wide view.
  Socket: `m_initiator` (initiator).
- **reset_gpio** -- System reset coordinator. Bridges QEMU
  system reset and SystemC reset.
  Sockets: `reset_out` (multi-initiator signal),
  `reset_in` (target signal).
- **sifive_test** -- SiFive test/shutdown device. Used for
  RISC-V system reset and poweroff.
  Sockets: `q_socket` (target), `initiator_socket`,
  `target_socket`, `reset` (signal port).

## QEMU/SystemC Parallelism

### TCG Threading Modes

QEMU/SystemC integration supports three threading mechanisms,
selected via the `tcg_mode` parameter on the QEMU instance:

| Value | Mode | Description |
|-------|------|-------------|
| `COROUTINE` | Single-thread, no parallelism | QEMU TCG does not run in parallel with the SystemC engine. Useful for determinism (with icount). |
| `SINGLE` | Single-thread, separate thread | QEMU TCG runs in a separate thread from SystemC. |
| `MULTI` | Multi-thread (default) | QEMU uses multiple TCG threads running in parallel with SystemC. Not all QEMU architectures support this. |

### icount Mode

QEMU supports icount mode where timing is based on
instruction count rather than wall-clock time.

| Parameter | Description |
|-----------|-------------|
| `icount` | Enable/disable icount mode |
| `icount_mips_shift` | MIPS shift value (1 insn = 2^shift ns). Setting this to non-zero also enables icount. |

The default is icount disabled. Without icount, QEMU uses
wall-clock time internally, which is non-deterministic.

icount mode must not be enabled with multi-threading
(`MULTI`), as QEMU does not support this combination.

### TLM2 Quantum Keeper Synchronization Mode

The synchronization library supports several policies (see
[libgssync](libgssync.md) for details):

| Policy | Compatible TCG Modes | Notes |
|--------|---------------------|-------|
| `tlm2` | `COROUTINE` only | Standard TLM2 mode. COROUTINE is assumed automatically. |
| `multithread` | `SINGLE`, `MULTI` | Basic multi-threaded, keeps within ~2 quantums. |
| `multithread-quantum` | `SINGLE`, `MULTI` | Closer to TLM behavior, waits for quantum boundary. This is the **default**. |
| `multithread-unconstrained` | `SINGLE`, `MULTI` | QEMU runs at its own pace. |

None of the `multithread` policies can be used with
`COROUTINE`.

For deterministic execution, enable **both** `tlm2`
synchronization **and** `icount` mode.

## Halt Interface

The halt interface manages the halt state of CPUs. By default,
halt is released (state 0). Halt does not work with
`p_power_off` set to `true`.
