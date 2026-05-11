.text
.global _start

_start:
    adr x2, mmio_addr
    ldr x2, [x2]                // x2 = MMIO_ADDR
    adr x1, dmi_addr
    ldr x1, [x1]                // x1 = DMI_ADDR
    adr x7, num_writes
    ldr x7, [x7]                // x7 = NUM_WRITES_PER_CPU

    mrs x0, mpidr_el1

    and x3, x0, #0xff
    and x0, x0, #0xff00
    lsr x0, x0, #5
    orr x0, x0, x3

    lsl x0, x0, #3
    add x1, x1, x0
    add x2, x2, x0

    // Init: will ask for a DMI pointer and go from ST_START to ST_READ_DMI
    ldr x0, [x1]
    str x0, [x2]

loop:
    // Read on DMI socket, report the read on the control socket
    ldr x0, [x1]
    str x0, [x2]

    add x0, x0, #1

    // Write on DMI socket, report the write on the control socket
    str x0, [x1]
    str x0, [x2]

    cmp x0, x7
    b.ne loop

end:
    ldr x5, =0x80000000
    str x5, [x2]
    wfi
    b end

.align 3
mmio_addr:
    .quad 0
dmi_addr:
    .quad 0
num_writes:
    .quad 0
