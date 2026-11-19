SECTIONS {
 . = 0x0;
 .text : { *(.text.entry) *(.text*) *(.rodata*) . = ALIGN(4); }
 /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment*) *(.note*) *(.got*) *(.rel*) }
}
