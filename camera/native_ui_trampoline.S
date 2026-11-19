.syntax unified
.thumb
.section .text.entry, "ax", %progbits
.balign 4
.global a6400_entry
.thumb_func
a6400_entry:
    b.w a6400_entry_trampoline
a6400_cfg:
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
a6400_entry_trampoline:
    adr r2, a6400_cfg
    b.w ui_release_loader
.balign 4
.global mt_original_release
.hidden mt_original_release
.thumb_func
mt_original_release:
    push.w {r4,r5,r6,r7,r8,r9,r10,lr}
    sub sp, #0x20
    mov.w r8, #0x14
    movs r2, #1
    ldr.w pc, a6400_cfg + 16
.section .note.GNU-stack,"",%progbits
