# nor_flash_backend

Native SystemC/TLM NOR flash backend for the QSPI controller.

## Architecture

```
┌─────────────────────┐        biflow_socket        ┌──────────────────────┐
│     QSPI module     │ ◄──────────────────────────► │   nor_flash_backend  │
│   (quic::qspi)      │                              │                      │
│                     │  last_write_fragment_signal  │  m_binary_content[]  │
│                     │ ─────────────────────────── ►│  selected profile    │
└─────────────────────┘                              │  (SFDP, ID, opcodes) │
                                                     └──────────────────────┘
```

The QSPI sends commands to the backend via `b_transport()`. The backend
processes them and enqueues response bytes back via `biflow_socket.enqueue()`.

The `last_write_fragment_signal` is driven by the QSPI to indicate whether
the current DMA write fragment is the last one in the chain. The backend uses
this to clear WEL (Write Enable Latch) only after the final fragment, matching
real NOR flash behaviour for multi-fragment DMA writes.

## Command set

| Opcode | Command | Behaviour |
|--------|---------|-----------|
| `0x03` | READ | Read from `m_binary_content` |
| `0x13` | READ4 | Read (4-byte address mode) |
| `0x0B/0x0C` | FAST_READ/FAST_READ4 | Fast read |
| `0x3B/0x3C` | DOUT_FAST_READ/DOUT_FAST_READ4 | Dual-output fast read |
| `0x6B/0x6C` | QOUT_FAST_READ/QOUT_FAST_READ4 | Quad-output fast read |
| `0xBB/0xBC` | DIO_FAST_READ/DIO_FAST_READ4 | Dual-I/O fast read |
| `0xEB/0xEC` | QIO_FAST_READ/QIO_FAST_READ4 | Quad-I/O fast read |
| `0xEC` | QIOR4 | Quad I/O read |
| `0x12` | PP4 | Page program (AND write into `m_binary_content`) |
| `0x34` | PP4_QUAD | Quad page program |
| `0x21` | ERASE_4K | Erase 4 KB sector (fill with `0xFF`) |
| `0x20` | ERASE_4K (3B) | Erase 4 KB sector |
| `0x52` | ERASE_32K | Erase 32 KB sector |
| `0xD8` | ERASE_SECTOR | Erase 64 KB sector |
| `0xDC` | ERASE4_SECTOR | Erase 64 KB sector (4-byte address) |
| `0xC7/0x60` | BULK_ERASE | Erase entire flash |
| `0x06` | WRITE_ENABLE | Set WEL in status register |
| `0x04` | WRITE_DISABLE | Clear WEL |
| `0x05` | READ_STATUS | Return status register |
| `0x70` | READ_FLAG_STATUS | Return flag status register |
| `0x65` | READ_ENHANCED_VOL_CFG | Return enhanced volatile config |
| `0x85` | READ_VOLATILE_CFG | Return volatile config |
| `0x9F` | READ_ID | Return device ID bytes |
| `0x5A` | READ_SFDP | Return SFDP table bytes |
| `0x01` | WRITE_STATUS | Write status register |
| `0x3E` | WRITE_STATUS_2 | Write MT25 status register 2 |
| `0x61` | WRITE_ENHANCED_VOL_CFG | Write enhanced volatile config |
| `0x81` | WRITE_VOLATILE_CFG | Write volatile config |
| `0xB7/0xE9` | ENTER/EXIT_4B_ADDR | MT35: update FSR address-mode bit; MT25: no-op |
| `0xB9/0xAB` | ENTER_DPD/RELEASE_DEEP_POWER_DOWN | No-op (acknowledged) |
| `0x66/0x99` | RESET_ENABLE/RESET | MT35: consecutive sequence reloads VCR from NVCR; MT25: no-op |
| `0x50` | CLEAR_FLAG_STATUS | Reset flag status register to `FSR_READY` |

### Write semantics

NOR flash is write-once per bit: `PP4` performs `memory[addr] &= data` (bits
can only be cleared, not set). Erase restores bits to `0xFF`.

WEL is cleared automatically after each write or erase operation. For DMA
multi-fragment writes, WEL is only cleared after the **last** fragment
(signalled by `last_write_fragment_signal = true`).

### MT35 flash reset

`Micron-MT35X-SCCR.pdf`, Rev. I (05/26), p. 37, Table 23 requires
RESET ENABLE followed by RESET MEMORY. The backend treats this as consecutive
flash commands: any intervening command, including a rejected command, cancels
the enable; another 66h re-arms it. An unenabled 99h is ignored.
The NVCR-to-VCR download is specified by Figure 6, p. 18 and Tables 6-7,
pp. 19-21. Reset copies all eight modeled bytes, preserves NVCR and array
contents, clears WEL/WIP, and preserves the nonvolatile status-register bits
(Table 3, p. 15). FSR errors/suspend bits clear and READY is set at completion
(Table 5, p. 17); its address-mode bit reflects NVCR05. VCR05 writes and B7h/E9h
(Table 36, p. 59) also update that bit. MT25 reset commands retain their
existing no-op behavior.

QSPI's reset signal and QMSPI's reset signal/`QMSPI_SW_RESET_LVL2` reset only
the controller; none sends 66h/99h or reloads flash configuration. The backend
has no physical reset input. Programs, erases and register writes complete
synchronously: there is no in-flight WRITE STATUS/NVCR interval in which 66h
could be rejected, no modeled reset timing (tSHSL2/tSHSL3), and no corruption
or suspend/volatile-lock state to simulate. READY can be read immediately via
70h; there is no asynchronous reset-completion polling requirement.
The reloaded I/O/address/dummy/XIP/wrap bytes are observable, but protocol,
dummy-cycle and XIP/wrap enforcement is not modeled. The controller's existing
address-byte tracker only follows B7h/E9h, not VCR/NVCR or flash reset; software
must configure its transfer framing explicitly. No cross-socket mode sync is
claimed.

## Lua configuration

Configured via `nor_flash_backend_func()` in `qup.lua`:

```lua
nor_flash_backend_func({
    binary_path = "/path/to/singleimage.bin",
    flash_type  = "qspi",                       -- "qspi" (MT25) or "ospi" (MT35XU01GBBA1G12-A0AAT)
})
```

| Parameter | Default | Description |
|-----------|---------|-------------|
| `binary_path` | `""` | Path to the NOR flash binary image |
| `flash_type` | `"qspi"` | Built-in MT25 QSPI or MT35X 1Gb OSPI profile |

### OSPI SFDP provenance

The OSPI profile is MT35XU01GBBA1G12-A0AAT: 1Gb, 1.8V, boot in SDR x1,
enhanced SFDP. Source: `SFDP-Micron-MT35X-SCCR.pdf`, internal title
`mt35x_SCCR_SFDP_addendum.pdf`, Rev. D (06/2026), part/options on p. 1,
Tables 1-6 on pp. 4-20. The 368-byte blob covers addresses 0x000-0x16F:

| Parameter ID | Pointer | DWORDs | Source pages |
|--------------|---------|--------|--------------|
| FF00 basic properties | 0x030 | 23 | 6-9 |
| FF84 4-byte commands | 0x090 | 2 | 10 |
| FF05 xSPI Profile 1.0 | 0x0A0 | 6 | 11 |
| FF0A octal DDR sequences | 0x0C0 | 8 | 12-13 |
| FF87 SCCR | 0x100 | 28 | 14-20 |

NPH is 4 (five headers); density is 0x3FFFFFFF bits-minus-one (128MiB).
All bitfields were re-encoded from the new document, not inherited from the
old blob. This includes timing/reset fields, 1.8V speed/dummy values, and
octal sequence descriptors. Documented padding remains FF, including
0x0B8-0x0BF and 0x0E0-0x0FF. The old xSPI/octal overlap is removed.
The JEDEC ID is `2C 5B 1B 10`, using `Micron-MT35X.pdf`, Table 18, p. 31
for the 1Gb capacity code; backend memory bounds match the SFDP density.

The addendum contains address/bit-label typos. Table 4's third DWORD labels
its first byte C8h rather than A8h; Table 6's seventh DWORD repeats 118h
for its second byte (119h), and its eleventh DWORD repeats 12Ah for its
fourth byte (12Bh). These are interpreted by their contiguous DWORD positions.
The ERASE ERROR byte 11Eh repeats bit 5 instead of bit 4; both printed
values are zero, interpreted as bits 5 and 4, consistent with PROGRAM ERROR.
No unsupported SCCR features or register behavior were added.

The pure framing helper recognizes both this SCCR header/1Gb ID and the
legacy non-SCCR header/2Gb ID. Controllers and generated register data are
unchanged. Regression: `qspi-nor-backend-integration-test` reads all 368
bytes through QSPI PIO, checks the full documented fixture, parameter
IDs/pointers/lengths/non-overlap, density and SCCR; `qspi-framing-test`
checks the new and legacy discovery headers.

The binary image is loaded once at `end_of_elaboration()` and kept in a
`std::vector<uint8_t>`. Writes and erases modify this in-memory buffer; the
file on disk is not modified.

`0xE8` and `0xB6` are intentionally rejected: their MT25 mode semantics are
not modelled. Unknown opcodes are rejected rather than acknowledged as no-ops.
