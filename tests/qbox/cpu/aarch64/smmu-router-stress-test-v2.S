// Combined firmware for the aarch64 SMMU router stress test v2.
// Three blobs at fixed offsets inside one flat binary:
//   0x0000  boot loader (jumps to main)
//   0x0200  diagnostic error handler
//   0x1000  main test logic
//
// All numeric constants (TESTER_ADDR, VIRTUAL_TEST_ADDR, MAX_ITERATIONS,
// BOUNDARY_BYTES, NUM_PAGES) are fixed by the test bench, so they are baked in
// here rather than patched at load time.

.set TESTER_ADDR,        0x40000
.set MAIN_FIRMWARE_ADDR, 0x1000
.set MAX_ITERATIONS,     0x7ff
.set VIRTUAL_TEST_ADDR,  0x80000000
.set BOUNDARY_BYTES,     80
.set NUM_PAGES,          2

.text
.global _start
.global boot_start

// --------------------------------------------------------------------------
// BOOT LOADER at 0x0
// --------------------------------------------------------------------------
.org 0x0
_start:
boot_start:
    movz x0, #MAIN_FIRMWARE_ADDR
    br   x0

// --------------------------------------------------------------------------
// DIAGNOSTIC ERROR HANDLER at 0x200
// --------------------------------------------------------------------------
.org 0x200
diagnostic_error:
    mrs x0, mpidr_el1
    and x0, x0, #0xff

    ldr x1, =TESTER_ADDR
    mov x2, #0x100
    mul x2, x0, x2
    add x1, x1, x2

    mov x2, #0xE000
    orr x2, x2, x0
    str x2, [x1, #0x28]

    mov x0, #1
    hlt #0

diagnostic_loop:
    wfi
    b diagnostic_loop

// --------------------------------------------------------------------------
// MAIN FIRMWARE at 0x1000
// --------------------------------------------------------------------------
.org 0x1000
main_start:
    mrs x0, mpidr_el1
    and x0, x0, #0xff
    mov x20, x0                   // x20 = CPU ID

    ldr x1, =TESTER_ADDR
    mov x2, #0x100
    mul x2, x20, x2
    add x21, x1, x2               // x21 = CPU-specific tester base

    mov x0, #0x1000
    str x0, [x21, #0x28]          // signal startup

    mov x22, #0                   // x22 = iteration counter

main_loop:
    mov x3, #MAX_ITERATIONS
    cmp x22, x3
    b.ge test_complete

    mov x0, #0x2000
    orr x0, x0, x22
    str x0, [x21, #0x28]          // debug: entering main loop

    mov x0, #1                    // FILL_REQUEST
    str x0, [x21, #0x00]

poll_fill:
    mov x0, #0x3000
    str x0, [x21, #0x28]          // debug: polling

    ldr x0, [x21, #0x08]          // REG_STATUS
    cmp x0, #1                    // READY?
    b.eq fill_ready
    cmp x0, #0                    // BUSY?
    b.eq try_check
    b poll_fill

fill_ready:
    ldr x23, [x21, #0x10]         // x23 = region ID

    mov x0, #0x4000
    orr x0, x0, x23
    str x0, [x21, #0x28]          // debug: starting work

    ldr x3, =VIRTUAL_TEST_ADDR    // x3 = VA base
    mov x4, #BOUNDARY_BYTES
    lsr x4, x4, #3                // x4 = words per boundary
    mov x26, #0                   // x26 = page number
    mov x19, #NUM_PAGES           // x19 = pages per region

fill_page_loop:
    cmp x26, x19
    b.ge fill_done

    mov x27, #1
    lsl x27, x27, #12             // x27 = 4KB
    mul x28, x26, x27             // x28 = page offset
    add x29, x3, x28              // x29 = page base

    mov x0, x29
    bl  fill_boundary

    mov x6, #1
    lsl x6, x6, #12
    sub x6, x6, x4, lsl #3
    add x0, x29, x6
    bl  fill_boundary

    add x26, x26, #1
    b   fill_page_loop

fill_done:
    mov x0, #1
    str x0, [x21, #0x18]          // REG_COMPLETE = FILL_DONE

    add x22, x22, #1
    b   main_loop

try_check:
    b main_loop

test_complete:
    b end

end:
    str x0, [x21, #0x30]
    wfi
    b end

// --------------------------------------------------------------------------
// fill_boundary: writes BOUNDARY_BYTES/8 pattern words starting at x0.
// Pattern encoding (read/write global regs x20=cpu, x23=region, x26=page,
// x4=word count, x5=word index/scratch):
//   bits 63:32 = CPU ID
//   bits 31:16 = region ID
//   bits 15:8  = page number
//   bits  7:0  = word offset
// --------------------------------------------------------------------------
fill_boundary:
    mov x5, #0
fill_boundary_loop:
    cmp x5, x4
    b.ge fill_boundary_done

    mov x6, #0

    lsl x7, x20, #32
    orr x6, x6, x7

    lsl x7, x23, #16
    orr x6, x6, x7

    lsl x7, x26, #8
    orr x6, x6, x7

    orr x6, x6, x5

    lsl x7, x5, #3
    add x7, x0, x7

    str x6, [x7]

    add x5, x5, #1
    b   fill_boundary_loop

fill_boundary_done:
    ret
