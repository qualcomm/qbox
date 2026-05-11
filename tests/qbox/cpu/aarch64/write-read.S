.text
.global _start

_start:
    adr x1, mmio_addr
    ldr x1, [x1]                // x1 = MMIO_ADDR
    adr x3, bulk_addr
    ldr x3, [x3]                // x3 = BULKMEM_ADDR
    adr x10, num_writes
    ldr x10, [x10]              // x10 = NUM_WRITES / p_num_cpu

    mrs x0, mpidr_el1

    and x2, x0, #0xff
    and x0, x0, #0xff00
    lsr x0, x0, #5
    orr x0, x0, x2

    lsl x0, x0, #3
    add x1, x1, x0

    mov x0, #0
    mov x9, x3
loop:
    str x0, [x9]
    add x9, x9, #8
    add x0, x0, #1
    cmp x0, x10
    b.ne loop

    mov x0, #0
    mov x9, x3
loop2:
    ldr x4, [x9]
    cmp x0, x4
    b.ne fail
    add x9, x9, #8
    add x0, x0, #1
    cmp x0, x10
    b.ne loop2

end:
    wfi
    mov x0, #0
    str x0, [x1]
    b end

fail:
    str x9, [x1]
    str x0, [x1]
    str x4, [x1]
    mov x0, #-1
    str x0, [x1]
    b end

.align 3
mmio_addr:
    .quad 0
bulk_addr:
    .quad 0
num_writes:
    .quad 0
