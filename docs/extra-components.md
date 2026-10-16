# Extra Components

The extra-components library provides complex components
including PCI devices and peripherals.

## GPEX (Generic PCI Express)

Add PCI devices to a GPEX host bridge using the `add_device`
method:

```cpp
m_gpex.add_device(m_disk1);
```

## NVME Disk

The `nvme` class provides an NVME disk that attaches to a GPEX
host bridge.

Constructor:
`nvme(const sc_core::sc_module_name& name, QemuInstance& inst, qemu_gpex* gpex)`

### CCI Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `serial` | component name | Serial name of the NVME disk |
| `blob_file` | (required) | File to use as data storage |
| `max_ioqpairs` | 64 | Maximum I/O queue pairs (passed to QEMU) |

The `blob_file` parameter is required -- the device will fail
to initialize without it.

## UFS (Universal Flash Storage)

The `ufs` class wraps the QEMU UFS Host Controller as a PCI
device that attaches to a GPEX host bridge. Individual storage
volumes are represented by logical units (`ufs_lu`), each of
which is added to the controller.

### UFS Host Controller

The `ufs` class provides the UFS Host Controller (QEMU).

#### CCI Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `serial` | string | `""` | Controller serial identifier |
| `nutrs` | uint32_t | 32 | Number of UTP transfer request slots |
| `nutmrs` | uint32_t | 8 | Number of UTP task management request slots |
| `mcq` | bool | `false` | Multiple command queue support |
| `mcq_maxq` | uint32_t | 2 | MCQ maximum number of queues |

#### Sockets

- `q_socket` -- Target socket for register access
- `irq_out` -- Interrupt signal output

### UFS Logical Unit

The `ufs_lu` class represents a UFS Logical Unit. It extends
`ufs::Device`. Each logical unit represents a storage volume
within the UFS controller.

#### CCI Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `lun` | uint32_t | 0 | Logical unit number |
| `blkdev_str` | string | `""` | Block device string for QEMU (do not specify ID) |

### Lua Configuration Example

```lua
ufs_0 = {
    moduletype = "ufs",
    args = {"&platform.qemu_inst"},
    -- mem.size is required: it sets the UFS MMIO aperture size.
    mem = {address = 0x1c140000, size = 0x10000, bind = "&router.initiator_socket"},
    irq_out = {bind = "&gic_0.spi_in_20"},
};

ufs_lu_0 = {
    moduletype = "ufs_lu",
    args = {"&platform.qemu_inst", "&platform.ufs_0"},
    lun = 0,
    blkdev_str = "file=disk.img,format=raw,if=none,readonly=off",
};
```

## OpenCores Ethernet MAC

The `opencores_eth` class wraps the QEMU OpenCores Ethernet MAC
device.

Constructor:
`opencores_eth(const sc_core::sc_module_name& n, QemuInstance& inst)`

### Sockets

- `regs_socket` -- Register access (target socket)
- `desc_socket` -- Descriptor access (target socket)
- `irq_out` -- Interrupt output

### CCI Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `mac` | `00:11:22:33:44:55` | MAC address |
| `netdev_str` | `user,hostfwd=tcp::2222-:22` | QEMU netdev string (do not specify ID) |

### Linux Kernel Configuration

Enable the driver:

```
CONFIG_ETHOC=y
Location:
  -> Device Drivers
    -> Network device support (NETDEVICES [=y])
      -> Ethernet driver support (ETHERNET [=y])
        -> OpenCores 10/100 Mbps Ethernet MAC support
```

### Device Tree Entry

```dts
ethoc: ethernet@c7000000 {
    compatible = "opencores,ethoc";
    reg = <0x00 0xc7000000 0x2000>;
    interrupts = <0x00 0x02 0x04>;
};
```
