.text
.globl boot_start
boot_start:
    // boot_start is the entry point for all Hexagon firmwares in Qbox tests
    // (see firmware.ld).
_start:
    // Load MMIO address from the constants section at the end of the binary
    r0 = memw(##mmio_addr)

    // Load and discard from MMIO_ADDR
    r2 = memw(r0)

    // Store 0x0f0f0f0f to MMIO_ADDR
    r3 = #0x0f0f0f0f
    memw(r0) = r3

end:
    // `wait(r0)` — encoded literally because some Hexagon versions of llvm-mc
    // don't accept the mnemonic; the bytes match the machine encoding.
    .word 0x6440c000
    jump end

.align 4
mmio_addr:
    .word 0
