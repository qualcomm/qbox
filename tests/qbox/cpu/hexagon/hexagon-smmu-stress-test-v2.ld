ENTRY(boot_start)

SECTIONS
{
    . = 0x0;
    
    .text : {
        *(.text)
        *(.text.*)
    }
    
    /* Ensure sufficient gap between code and data */
    . = ALIGN(256);
    
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }
    
    . = ALIGN(16);
    
    .data : {
        *(.data)
        *(.data.*)
    }
    
    . = ALIGN(16);
    
    .bss : {
        *(.bss)
        *(.bss.*)
    }
}