.text
.global _start

_start:
    adr x1, mmio_addr
    ldr x1, [x1]                // x1 = MMIO_ADDR

loop:
    ldxr x0, [x1]
    stxr w2, x0, [x1]

    // Make sure the exclusive store failed
    cmp w2, #0
    beq fail

end:
    wfi
    b end

fail:
    str x0, [x1]
    b end

.align 3
mmio_addr:
    .quad 0
