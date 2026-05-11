.text
.global _start

_start:
    adr x1, mmio_addr
    ldr x1, [x1]                // x1 = MMIO_ADDR
    adr x6, num_writes
    ldr x6, [x6]                // x6 = NUM_WRITES (=NUM_WRITES * 2)

    mrs x0, mpidr_el1

    and x2, x0, #0xff
    and x0, x0, #0xff00
    lsr x0, x0, #5
    orr x0, x0, x2

    lsl x0, x0, #3
    add x1, x1, x0

    mov x0, #0

loop:
    str x0, [x1]
    add x0, x0, #1
    cmp x0, x6
    b.ne loop

end:
    wfi
    b end

.align 3
mmio_addr:
    .quad 0
num_writes:
    .quad 0
