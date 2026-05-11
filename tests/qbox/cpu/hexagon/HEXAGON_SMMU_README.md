# Hexagon SMMU Stress Test

This directory contains the Hexagon version of the SMMU router stress test v2.

## Files

- `hexagon-smmu-stress-test-v2.s` - Hexagon assembly firmware (translated from ARM64)
- `hexagon-smmu-stress-test-v2.cc` - C++ test bench
- `HEXAGON_SMMU_README.md` - This file

## Hexagon MMU Configuration

### Overview
The Hexagon CPU includes a built-in MMU that can translate 32-bit virtual addresses to physical addresses exceeding 32 bits. This enables the pipeline: CPU (32-bit VA) → Hexagon MMU (>32-bit PA) → TBU/SMMU.

### MMU Enable Control
- **SYSCFG.MMUEN** (bit 0): Controls MMU enable/disable
- When SYSCFG.MMUEN transitions 0→1: `hex_mmu_on()` is called (TLB flush)
- When SYSCFG.MMUEN transitions 1→0: `hex_mmu_off()` is called (TLB flush)

### ASID Configuration
- **SSR.ASID** (bits 8..14): Address Space ID for TLB matching
- TLB entries match against SSR.ASID unless marked Global (PTE_G=1)
- For initial bring-up: use PTE_G=1 and SSR.ASID=0 to avoid ASID management

### TLB Entry Format
Key fields for >32-bit physical addressing:
- **PTE_V** (bit 63): Valid bit
- **PTE_G** (bit 62): Global (ignores ASID)
- **PTE_HSV39** (bit 27): Enable extended physical addressing mode
- **PTE_VPN** (bits 32..51): Virtual page number
- **PTE_X/R/W/U**: Execute/read/write/user permissions
- **Physical Address Encoding**:
  - When PTE_HSV39=1: PA uses PTE_PPD (bits 0..23) + PTE_PA43 + PTE_PA4544
  - Supports physical addresses beyond 32 bits (up to 45 bits)

### TLB Management Instructions
- **tlbw**: Write TLB entry
- **tlbp**: Probe TLB for index
- **tlbr**: Read TLB entry
- **tlblock/tlbunlock**: Atomic TLB operations for multi-thread safety

### Firmware Enablement Sequence
1. Set SSR.ASID (optional for Global entries)
2. Build TLB entry mapping VA to >32-bit PA:
   - Set PTE_V=1, PTE_G=1, PTE_HSV39=1
   - Configure VPN for VA page
   - Set PA across PTE_PPD + PTE_PA43 + PTE_PA4544
   - Set permissions (X/R/W/U)
3. Write entry using `tlbw` instruction
4. Enable MMU: Set SYSCFG.MMUEN=1
5. Issue `isync`/`syncht` for synchronization

### Example Mapping
- VA 0x80000000 → PA 0x300000000 (12GB)
- Ensures MMU emits >32-bit PAs visible to TBU/SMMU
- SMMU then applies StreamID/Context Bank translation to reach final memory

### QEMU Implementation References
- MMU enable/disable: `target/hexagon/op_helper.c`
- TLB format/translation: `target/hexagon/hex_mmu.c`
- Register bitfields: `target/hexagon/reg_fields_def.h.inc`
- System instructions: `target/hexagon/imported/system.idef`

## Compilation Instructions

### Prerequisites

You need the Hexagon SDK installed with the following tools:
- `hexagon-clang` - Hexagon C/C++ compiler
- `hexagon-as` - Hexagon assembler
- `hexagon-objcopy` - Object file converter
- `hexagon-objdump` - Object file disassembler (for verification)

### Step 1: Assemble the Firmware

```bash
# Assemble the .s file to object file
hexagon-clang -march=hexagon -c hexagon-smmu-stress-test-v2.s -o hexagon-smmu-stress-test-v2.o

# Or use the assembler directly:
hexagon-as -march=hexagon hexagon-smmu-stress-test-v2.s -o hexagon-smmu-stress-test-v2.o
```

### Step 2: Extract Binary

```bash
# Convert object file to raw binary
hexagon-objcopy -O binary hexagon-smmu-stress-test-v2.o hexagon-smmu-stress-test-v2.bin
```

### Step 3: Generate C Array (Optional)

```bash
# Generate hex dump for embedding in C++
hexdump -v -e '16/1 "0x%02x, " "\n"' hexagon-smmu-stress-test-v2.bin > hexagon-smmu-stress-test-v2.hex
```

### Step 4: Verify (Optional)

```bash
# Disassemble to verify instructions
hexagon-objdump -d hexagon-smmu-stress-test-v2.o > hexagon-smmu-stress-test-v2.dis

# Check the disassembly to ensure instructions are correct
cat hexagon-smmu-stress-test-v2.dis
```

## Integration with C++ Test

### Option 1: Load Binary File Directly

```cpp
// In your test bench constructor
std::ifstream firmware_file("hexagon-smmu-stress-test-v2.bin", std::ios::binary);
std::vector<uint8_t> firmware_data(
    (std::istreambuf_iterator<char>(firmware_file)),
    std::istreambuf_iterator<char>()
);
m_mem.load.ptr_load(firmware_data.data(), MEM_ADDR, firmware_data.size());
```

### Option 2: Embed as C Array

```cpp
// Include the generated hex file
static const uint8_t HEXAGON_FIRMWARE[] = {
    #include "hexagon-smmu-stress-test-v2.hex"
};

// Load into memory
m_mem.load.ptr_load(HEXAGON_FIRMWARE, MEM_ADDR, sizeof(HEXAGON_FIRMWARE));
```

### Option 3: Load Sections Separately

```cpp
// Extract individual sections from object file
hexagon-objcopy -O binary --only-section=.text hexagon-smmu-stress-test-v2.o boot.bin

// Load each section at its specific address
m_mem.load.ptr_load(boot_data, BOOT_ADDR, boot_size);
m_mem.load.ptr_load(diagnostic_data, DIAGNOSTIC_ADDR, diagnostic_size);
m_mem.load.ptr_load(main_data, MAIN_FIRMWARE_ADDR, main_size);
```

## Memory Layout

The firmware expects the following memory layout:

| Address Range | Description | Size |
|--------------|-------------|------|
| 0x00000000 - 0x0003FFFF | Firmware memory | 256KB |
| 0x00000000 | Boot loader | ~16 bytes |
| 0x00000200 | Diagnostic handler | ~64 bytes |
| 0x00001000 | Main firmware | ~2KB |
| 0x00040000 - 0x0004FFFF | Tester MMIO | 64KB |
| 0x00050000 - 0x0006FFFF | SMMU registers | 128KB |
| 0x10000000 - 0x1FFFFFFF | Main memory | 256MB |
| 0x300000000 | Virtual test region | (mapped by SMMU) |

## Register Usage

The firmware uses the following register conventions:

### Persistent Registers (Callee-saved)
- `r16` - CPU ID
- `r17` - Tester base address
- `r18` - Iteration counter
- `r19` - Region ID
- `r20` - Page number
- `r21` - Number of pages per region

### Temporary Registers (Caller-saved)
- `r0-r15` - Scratch/temporary
- `r31` - Link register (return address)

## MMIO Register Layout (Tester Interface)

Each CPU has a 256-byte (0x100) register space:

| Offset | Name | Direction | Description |
|--------|------|-----------|-------------|
| 0x00 | REG_REQUEST | Write | Request type (1=fill, 2=check) |
| 0x08 | REG_STATUS | Read | Status (0=busy, 1=ready, 2=complete) |
| 0x10 | REG_REGION_ID | Read | Assigned region ID |
| 0x18 | REG_COMPLETE | Write | Completion (1=fill_done, 2=check_done) |
| 0x20 | REG_ITERATIONS | Read | Current iteration count |
| 0x28 | REG_DEBUG | Write | Debug messages |
| 0x30 | REG_DEBUG_DATA | Write | Debug data |

## Pattern Format

The firmware fills memory with a 64-bit pattern:

```
Bits 63:32 - CPU ID (32-bit)
Bits 31:16 - Region ID (16-bit)
Bits 15:8  - Page number (8-bit)
Bits 7:0   - Word offset (8-bit)
```

This pattern allows verification that:
1. The correct CPU filled the region
2. The correct region was filled
3. The correct page within the region was filled
4. The data is in the correct order

## Troubleshooting

### Assembly Errors

If you get assembly errors, check:
1. Hexagon SDK version compatibility
2. Instruction syntax (may vary between versions)
3. Register usage (ensure no conflicts)
4. Immediate value ranges (some instructions have limits)

### Runtime Issues

If the test fails at runtime:
1. Verify memory layout matches expectations
2. Check that SMMU is configured correctly
3. Ensure Hexagon CPU parameters are set properly
4. Use debug messages (REG_DEBUG) to trace execution
5. Verify the binary was loaded at correct addresses

### CPU ID Retrieval

The current implementation has a placeholder for CPU ID retrieval:
```hexagon
r16 = #0  // TODO: Get actual CPU/thread ID
```

You may need to:
1. Pass CPU ID via a known memory location
2. Use Hexagon-specific system registers
3. Configure via `hexagon_globalreg` component
4. Use thread ID from multi-threading support

## Notes

- This firmware is designed for Hexagon v67 or later
- Some instructions may need adjustment for different Hexagon versions
- The SMMU configuration is CPU-agnostic and doesn't need changes
- Extended immediates (##) are used for 32-bit values
- 64-bit operations use register pairs (r1:0, r3:2, etc.)

## Next Steps

1. Compile the assembly file
2. Create C++ test bench (copy from ARM version)
3. Replace ARM CPU with Hexagon CPU
4. Load firmware binary instead of using Keystone
5. Add hexagon_globalreg component
6. Update CMakeLists.txt
7. Build and test
