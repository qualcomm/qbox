.text
.globl boot_start
boot_start:
_start:
    // r0 = MMIO address, r1 = trigger/done value, r2 = done value
    r0 = memw(##mmio_addr)
    r1 = memw(##trigger_val)
    r2 = memw(##done_val)
    p0 = cmp.eq(r1, r2)
    // `if (p0) jump reset_done`  — encoded as the bytes shown; some Hexagon
    // llvm-mc versions reject the plain mnemonic for this predicate form.
    .word 0x5c00c006

reset:
    memw(r0) = r1
    // `jump .`  (infinite loop)
    .word 0x5800c000

reset_done:
    memw(r0) = r1
    // `wait(r0)`  (see ld-st-mmio.S for why this is encoded directly)
    .word 0x6440c000

.align 4
mmio_addr:
    .word 0
trigger_val:
    .word 0
done_val:
    .word 0
