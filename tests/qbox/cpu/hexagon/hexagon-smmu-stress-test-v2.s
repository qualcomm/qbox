/*
 * Hexagon SMMU Stress Test Firmware
 * Translated from ARM64 version in smmu-router-stress-test-v2.cc
 *
 * This firmware runs on Hexagon CPUs behind SMMU TBUs, testing address
 * translation and multi-CPU memory access patterns using DMA operations.
 *
 * Memory Layout:
 *   0x00000000: Boot loader (jumps to main firmware)
 *   0x00000200: Diagnostic error handler
 *   0x00001000: Main firmware (test logic)
 *
 * Register Usage:
 *   r16: CPU ID (persistent)
 *   r17: Tester base address (persistent)
 *   r18: Iteration counter (persistent)
 *   r19: Region ID (persistent)
 *   r20: Page number (persistent)
 *   r21: Number of pages per region (persistent)
 *   r24: Current DMA descriptor address pointer (persistent)
 *   r25: DMA descriptor tail pointer for dmlink (persistent)
 *   r26: DMA descriptor counter (persistent)
 *   r0-r15: Temporary/scratch registers
 *   r31: Link register (return address)
 */

// Configuration constants (can be overridden by --defsym)
.set TESTER_ADDR, 0x40000
.set REG_REQUEST, 0x00
.set REG_STATUS, 0x08
.set REG_REGION_ID, 0x10
.set REG_COMPLETE, 0x18
.set REG_DEBUG, 0x28
.set REG_DEBUG_DATA, 0x30
.set REG_CPU_ID, 0x48
.set MAX_ITERATIONS, 0x7fff
.set VIRTUAL_TEST_ADDR_LOW, 0x80000000
.set BOUNDARY_BYTES, 80
.set PAGE_SIZE, 0x1000

// DMA descriptor constants
.set DMA_DESC_SIZE, 16
.set DESCRIPTORS_PER_CPU, 4      // 2 pages × 2 boundaries = 4 descriptors
.set DESC_BUFFER_PER_CPU, 0x40   // 4 descriptors × 16 bytes = 64 bytes

    .text
    .align 4

/*
 * ============================================================================
 * BOOT LOADER at 0x00000000
 * ============================================================================
 */
    .org 0x0
    .globl boot_start
boot_start:
    jump main_start

/*
 * ============================================================================
 * DIAGNOSTIC ERROR HANDLER at 0x00000200
 * ============================================================================
 * Reports error to tester and halts. Testbench extracts CPU ID from pathid.
 */
    .org 0x200
    .globl diagnostic_error
diagnostic_error:
    r17 = #TESTER_ADDR      // Fixed tester base address for all CPUs
    r2 = #0xE000            // Error code (diagnostic error)
    memw(r17 + #REG_DEBUG) = r2
diagnostic_loop:
    jump diagnostic_loop

/*
 * ============================================================================
 * MMU INITIALIZATION
 * ============================================================================
 * Enables MMU and sets up TLB entries:
 *   - High-VA mapping: VA 0x80000000 -> PA 0x300000000 (1M pages, HSV39=1)
 *   - Identity mapping: VA 0 -> PA 0 (256M pages, HSV39=0)
 */
    .globl mmu_init
mmu_init:
    // High-VA mapping: VA 0x80000000 -> PA 0x300000000
    // Uses 1M pages (size index 4) with HSV39=1
    // QEMU shifts by 12 bits: PPD = (0x300000 << 1) | 1 = 0x600001
    r0 = #0x600001
    r0 = or(r0, #(0xf << 28))  // U,R,W,X permissions
    r0 = or(r0, #(1 << 27))    // HSV39=1

    r1 = #(0x800 << 0)         // VPN for VA 0x80000000
    r1 = or(r1, #(3 << 30))    // G=1, V=1
    r1 = or(r1, #(0 << 27))    // PA4544 = 00b

    r3 = #0
    tlbw(r1:0, r3)

    // Identity map for low 2GB (firmware code and tester registers)
    // Uses 256M pages (size index 8) with HSV39=0
    r0 = #(1 << 8)             // Bit 8 set for ctz64=8
    r0 = or(r0, #(0xf << 28))  // U,R,W,X permissions

    r1 = #(0)                  // VPN=0
    r1 = or(r1, #(3 << 30))    // G=1, V=1

    r3 = #1
    tlbw(r1:0, r3)

    // Enable MMU in SYSCFG register
    r3 = syscfg
    r3 = or(r3, #1)         // Set MMUEN bit (bit 0)
    syscfg = r3
    isync

    jumpr r31

/*
 * ============================================================================
 * MAIN FIRMWARE at 0x00001000
 * ============================================================================
 */
    .org 0x1000
    .globl main_start
main_start:
    call mmu_init

    // Use fixed tester address (testbench extracts CPU ID from pathid)
    r17 = #TESTER_ADDR
    r16 = memw(r17 + #REG_CPU_ID)  // r16 = global CPU ID

    // Signal startup
    r0 = #0x1000            // Startup debug message
    memw(r17 + #REG_DEBUG) = r0

    // Calculate per-CPU DMA descriptor buffer base
    r24 = ##dma_descriptor_buffer
    r1 = #DESC_BUFFER_PER_CPU
    r1 = mpyi(r1, r16)
    r24 = add(r24, r1)

    // Initialize DMA descriptor tracking
    r25 = #0                       // Tail pointer = 0 (no previous descriptor)
    r26 = #0                       // Descriptor counter = 0

    // Initialize iteration counter
    r18 = #0                // r18 = iteration counter
    r2 = #MAX_ITERATIONS

/*
 * ============================================================================
 * MAIN LOOP
 * ============================================================================
 */
main_loop:
    p0 = cmp.eq(r18, r2)
    if (p0) jump test_complete

    // Signal entering main loop
    r0 = #0x2000
    r0 = add(r0, r18)
    memw(r17 + #REG_DEBUG) = r0

    // Request a region to fill
    r0 = #1
    memw(r17 + #REG_REQUEST) = r0

/*
 * Poll for readiness - wait for tester to assign a region
 */
 poll_fill:
    r0 = #0x3000            // Polling debug message
    memw(r17 + #REG_DEBUG) = r0

    r0 = memw(r17 + #REG_STATUS)
    p0 = cmp.eq(r0, #1)
    if (p0) jump fill_ready

    jump poll_fill

/*
 * Region is ready - fill it with pattern
 */
fill_ready:
    r19 = memw(r17 + #REG_REGION_ID)

    r0 = #0x4000            // Starting work debug message
    r0 = add(r0, r19)
    memw(r17 + #REG_DEBUG) = r0

    r0 = #0x6000
    r0 = add(r0, r19)
    memw(r17 + #REG_DEBUG) = r0

    // DMA uses internal virtual address (0x80000000)
    // Hexagon MMU translates: 0x80000000 -> 0x300000000 (external VA)
    // SMMU then translates: 0x300000000 -> physical address
    r2 = #VIRTUAL_TEST_ADDR_LOW

    r4 = #BOUNDARY_BYTES
    r4 = lsr(r4, #3)        // Convert to 8-byte words (divide by 8)

    // Initialize page loop
    r20 = #0                // Page number (starts at 0)
    r21 = #2                // Number of pages per region (8KB / 4KB = 2)

/*
 * Loop over pages in the region
 */
fill_page_loop:
    p0 = cmp.eq(r20, r21)
    if (p0) jump fill_done

    // Calculate page base address
    r8 = #PAGE_SIZE
    r8 = mpyi(r8, r20)      // page_offset = page_num * PAGE_SIZE
    r9 = r2                 // Base address (VIRTUAL_TEST_ADDR_LOW)
    r9 = add(r9, r8)       // Add page offset

    // Fill start boundary
    r0 = r9
    call fill_boundary_dma

    // Calculate end boundary address
    r6 = #PAGE_SIZE
    r7 = asl(r4, #3)        // words * 8 (convert back to bytes)
    r6 = sub(r6, r7)        // offset = PAGE_SIZE - (words * 8)
    r0 = r9                 // Restore page base
    r0 = add(r0, r6)       // Add end offset

    // Fill end boundary
    call fill_boundary_dma

    r20 = add(r20, #1)
    jump fill_page_loop

fill_done:
//    r0 = #0xD400
//    memw(r17 + #REG_DEBUG_DATA) = r0
//    memw(r17 + #REG_DEBUG_DATA) = r26

    // Wait for all chained DMA operations to complete
    r0 = dmwait

//    r1 = #0xD450
//    memw(r17 + #REG_DEBUG_DATA) = r1
//    memw(r17 + #REG_DEBUG_DATA) = r0

    // Check for non-zero status (0 = DM0_STATUS_IDLE = success)
    p0 = cmp.eq(r0, #0)
    if (!p0) jump dma_chain_error

    r0 = #0xD500            // DMA chain complete debug code
    memw(r17 + #REG_DEBUG_DATA) = r0

    // Reset descriptor pointers for next region
    r1 = ##dma_descriptor_buffer
    r2 = #DESC_BUFFER_PER_CPU
    r2 = mpyi(r2, r16)            // CPU_ID × 64
    r24 = add(r1, r2)             // r24 = per-CPU descriptor base (reset)

    // Clear descriptor buffer to remove stale next pointers
    /*r0 = #0
    r1 = #0
    memd(r24 + #0) = r1:0
    memd(r24 + #8) = r1:0
    memd(r24 + #16) = r1:0
    memd(r24 + #24) = r1:0
    memd(r24 + #32) = r1:0
    memd(r24 + #40) = r1:0
    memd(r24 + #48) = r1:0
    memd(r24 + #56) = r1:0
    barrier
    */
    r25 = #0
    r26 = #0

    // Notify tester
    r0 = #1
    memw(r17 + #REG_COMPLETE) = r0

    r18 = add(r18, #1)
    jump main_loop

dma_chain_error:
    r1 = #0xDE10
    r1 = add(r1, r0)
    memw(r17 + #REG_DEBUG_DATA) = r1
    memw(r17 + #REG_DEBUG_DATA) = r0
    memw(r17 + #REG_DEBUG_DATA) = r26
    jump diagnostic_error

test_complete:
    jump end

end:
    jump end

/*
 * ============================================================================
 * FILL_BOUNDARY_DMA FUNCTION
 * ============================================================================
 * Fills a memory boundary using DMA transfer from local staging buffer.
 *
 * Steps:
 * 1. Fill per-descriptor staging buffer with pattern
 * 2. Set up DMA descriptor (Type 0 - Linear)
 * 3. Link descriptor using dmlink (chains without waiting)
 *
 * Pattern format (64-bit):
 *   Bits 63:32 - CPU ID
 *   Bits 31:16 - Region ID
 *   Bits 15:8  - Page number
 *   Bits 7:0   - Word offset
 *
 * Arguments:
 *   r0 - Destination address (32-bit internal VA, will be translated by MMU/SMMU)
 *
 * Uses globals: r16 (CPU ID), r17 (tester base), r19 (region ID), r20 (page num), r4 (num words)
 * Clobbers: r0-r15, r22-r27
 */
    .globl fill_boundary_dma
fill_boundary_dma:
    r22 = r31
    r23 = r0

    // Calculate per-descriptor staging buffer address
    // Each descriptor needs its own buffer: 4 descriptors × 128 bytes = 512 bytes per CPU
    r0 = ##local_staging_buffer
    r1 = #0x200                  // 512 bytes per CPU (4 descriptors × 128 bytes)
    r1 = mpyi(r1, r16)           // CPU_ID × 512
    r0 = add(r0, r1)             // r0 = per-CPU base

    r1 = #0x80
    r1 = mpyi(r1, r26)           // descriptor_counter × 128
    r0 = add(r0, r1)             // r0 = unique staging buffer for this descriptor
    r27 = r0

    // Fill staging buffer with pattern
    call fill_boundary

    // Set up DMA descriptor (Type 0 - Linear)
    // Word 0: next pointer (NULL - will be set by dmlink)
    r0 = #0
    memw(r24 + #0) = r0

    // Word 1: control word
    // Bits [23:0]: length, Bits [25:24]: desctype=0
    r0 = #BOUNDARY_BYTES
    memw(r24 + #4) = r0

    // Word 2: source address (staging buffer)
    memw(r24 + #8) = r27

    // Word 3: destination address (internal VA)
    memw(r24 + #12) = r23

/*    // Debug: Log all descriptor words
    r0 = #0xD101
    memw(r17 + #REG_DEBUG_DATA) = r0
    r0 = memw(r24 + #0)
    memw(r17 + #REG_DEBUG_DATA) = r0

    r0 = #0xD102
    memw(r17 + #REG_DEBUG_DATA) = r0
    r0 = memw(r24 + #4)
    memw(r17 + #REG_DEBUG_DATA) = r0

    r0 = #0xD103
    memw(r17 + #REG_DEBUG_DATA) = r0
    r0 = memw(r24 + #8)
    memw(r17 + #REG_DEBUG_DATA) = r0

    r0 = #0xD104
    memw(r17 + #REG_DEBUG_DATA) = r0
    r0 = memw(r24 + #12)
    memw(r17 + #REG_DEBUG_DATA) = r0

    r0 = #0xD100
    memw(r17 + #REG_DEBUG_DATA) = r0
*/
    //barrier

    // Link descriptor (NO WAIT - chains them together)
    r0 = r25
    r1 = r24
    dmlink(r0, r1)

    //r0 = #0xD200
    //memw(r17 + #REG_DEBUG_DATA) = r0
    //memw(r17 + #REG_DEBUG_DATA) = r25
    //memw(r17 + #REG_DEBUG_DATA) = r24

    // Update tail pointer and counters
    r25 = r24
    r26 = add(r26, #1)
    r24 = add(r24, #DMA_DESC_SIZE)

    r31 = r22
    jumpr r31

/*
 * ============================================================================
 * FILL_BOUNDARY FUNCTION
 * ============================================================================
 * Fills a memory boundary with a verifiable pattern.
 *
 * Pattern format (64-bit):
 *   Bits 63:32 - CPU ID
 *   Bits 31:16 - Region ID
 *   Bits 15:8  - Page number
 *   Bits 7:0   - Word offset
 *
 * Arguments:
 *   r0 - Base address to start writing
 *
 * Uses globals: r16 (CPU ID), r19 (region ID), r20 (page num), r4 (num words)
 * Clobbers: r5-r9
 */
    .globl fill_boundary
fill_boundary:
    r5 = #0

fill_boundary_loop:
    p0 = cmp.eq(r5, r4)
    if (p0) jump fill_boundary_done

    // Build pattern: (CPU_ID << 32) | (REGION_ID << 16) | (PAGE_NUM << 8) | WORD_OFFSET
    r7 = r16                // CPU ID
    r6 = #0

    r8 = asl(r19, #16)
    r6 = or(r6, r8)

    r8 = asl(r20, #8)
    r6 = or(r6, r8)

    r6 = or(r6, r5)

    // Calculate write address
    r8 = asl(r5, #3)
    r8 = add(r0, r8)

     //memd(r17 + #REG_DEBUG) = r7:6
     //memw(r17 + #REG_DEBUG + 8) = r8

    memd(r8) = r7:6

    r5 = add(r5, #1)
    jump fill_boundary_loop

fill_boundary_done:
    jumpr r31

/*
 * ============================================================================
 * DMA BUFFERS IN TEXT SECTION
 * ============================================================================
 * Placed in .text section at known addresses within identity-mapped region
 * for accessibility to both CPU and DMA engine.
 */
    .org 0x3000
    .align 4

/*
 * DMA DESCRIPTOR BUFFER (Per-CPU allocation)
 * 32 CPUs × 64 bytes each = 2048 bytes (0x800)
 */
    .globl dma_descriptor_buffer
dma_descriptor_buffer:
    .space 0x800

/*
 * LOCAL STAGING BUFFER (Per-descriptor allocation)
 * Each descriptor needs its own buffer to avoid race conditions
 * 32 CPUs × 512 bytes each = 16384 bytes (0x4000)
 */
    .align 4
    .globl local_staging_buffer
local_staging_buffer:
    .space 0x4000

/*
 * ============================================================================
 * END OF FIRMWARE
 * ============================================================================
 */
