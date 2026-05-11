.text
.global _start

_start:
    adr x2, mmio_addr
    ldr x2, [x2]                // x2 = MMIO_ADDR
    adr x1, dmi_addr
    ldr x1, [x1]                // x1 = DMI_ADDR
    adr x3, num_writes
    ldr x3, [x3]                // x3 = NUM_WRITES_PER_CPU

    mrs x0, mpidr_el1

    and x7, x0, #0xff
    and x0, x0, #0xff00
    lsr x0, x0, #5
    orr x0, x0, x7

    lsl x0, x0, #3
    add x1, x1, x0
    add x2, x2, x0

    mov x4, #0

loop:
    // Read/Write on DMI socket
    ldr x0, [x1]
    cmp x0, x4
    b.ne fail
    add x0, x0, #1
    str x0, [x1]
    mov x4, x0

    // Read/Write on DMI socket
    ldr x0, [x1]
    cmp x0, x4
    b.ne fail
    add x0, x0, #1
    str x0, [x1]
    mov x4, x0

    // Do DMI invalidation
    str x0, [x2]

    // Read/Write on DMI socket
    ldr x0, [x1]
    cmp x0, x4
    b.ne fail
    add x0, x0, #1
    str x0, [x1]
    mov x4, x0

    // Read/Write on DMI socket
    ldr x0, [x1]
    cmp x0, x4
    b.ne fail
    add x0, x0, #1
    str x0, [x1]
    mov x4, x0

    cmp x0, x3
    b.lt loop

end:
    ldr x5, =0x80000000
    str x5, [x2]
    wfi
    b end

fail:
    mov x0, #-1
    str x0, [x2]
    b end

.align 3
mmio_addr:
    .quad 0
dmi_addr:
    .quad 0
num_writes:
    .quad 0
