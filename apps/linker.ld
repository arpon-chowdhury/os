ENTRY(_start)
PHDRS {
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
    stack PT_GNU_STACK FLAGS(6);
}
SECTIONS {
    . = 0x400000;
    .text : { *(.text*) *(.rodata*) } :text
    . = 0x600000;
    .data : { *(.data*) } :data
    .bss (NOLOAD) : { *(.bss*) *(COMMON) } :data
    /DISCARD/ : { *(.note*) *(.eh_frame*) *(.comment*) }
}
