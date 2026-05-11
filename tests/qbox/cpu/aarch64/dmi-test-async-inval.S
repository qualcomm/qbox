.text
.global _start

_start:
    adr x2, mmio_addr
    ldr x2, [x2]                // x2 = MMIO_ADDR
    adr x1, dmi_addr
    ldr x1, [x1]                // x1 = DMI_ADDR
    adr x5, dmi_size_mask
    ldr x5, [x5]                // x5 = DMI_SIZE-1
    adr x6, rng_seed
    ldr x6, [x6]                // x6 = random seed
    adr x3, num_writes
    ldr x3, [x3]                // x3 = NUM_WRITES_PER_CPU

    mrs x0, mpidr_el1

    and x10, x0, #0xff
    and x0, x0, #0xff00
    lsr x0, x0, #5
    orr x0, x0, x10
    lsl x0, x0, #3

    add x2, x2, x0
    ror x6, x6, x0

loop:
    mov x0, #1
    str x0, [x2]
    str x3, [x2]

    mov x10, x6
    ror x10, x10, #17
    eor x6, x6, x10
    ror x10, x10, #54
    eor x6, x6, x10
    ror x10, x10, #29
    eor x6, x6, x10
    mov x6, x10

    and x10, x10, x5

    add x10, x10, x1
    and x10, x10, #-8
    str x10, [x10]

    mov x0, #2
    str x0, [x2]
    str x10, [x2]

    ldr x7, [x10]
    cmp x10, x7
    b.ne fail

    sub x3, x3, #1
    cmp x3, #0
    b.ne loop

end:
    wfi
    mov x0, #0
    str x0, [x2]
    b end

fail:
    mov x0, #-2
    str x0, [x2]
    b end

.align 3
mmio_addr:
    .quad 0
dmi_addr:
    .quad 0
dmi_size_mask:
    .quad 0
rng_seed:
    .quad 0
num_writes:
    .quad 0
