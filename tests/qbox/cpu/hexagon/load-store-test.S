.text
.globl boot_start
boot_start:
_start:
    r0 = memw(##mmio_addr)

    // load and discard the result from MMIO_ADDR
    r2 = memw(r0)

    // store 0x0f0f0f0f to MMIO_ADDR
    r3 = #0x0f0f0f0f
    memw(r0) = r3

end:
    // wait(r0) — encoded literally, see ld-st-mmio.S
    .word 0x6440c000
    jump end

.align 4
mmio_addr:
    .word 0
