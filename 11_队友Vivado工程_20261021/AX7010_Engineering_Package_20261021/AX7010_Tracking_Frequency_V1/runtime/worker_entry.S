.syntax unified
.arm
.section .text.entry,"ax"
.global _start
_start:
    cpsid if
    ldr sp, =0x02e00000
    mov r0, #0
    mcr p15,0,r0,c7,c5,0
    mcr p15,0,r0,c8,c7,0
    dsb sy
    isb
    /* XSDB download does not substitute for C runtime BSS initialisation.
     * Firmware switching must not inherit old geometry/filter state. */
    ldr r1, =__bss_start__
    ldr r2, =__bss_end__
    mov r0, #0
2:
    cmp r1, r2
    bhs 3f
    str r0, [r1], #4
    b 2b
3:
    /* C may save NEON callee-saved registers in its prologue, before the
     * first C statement. Enable CP10/CP11 and FPEXC before entering C. */
    mrc p15,0,r0,c1,c0,2
    orr r0,r0,#0x00f00000
    mcr p15,0,r0,c1,c0,2
    isb
    mov r0,#0x40000000
    vmsr fpexc,r0
    bl worker_boot
1:  b 1b
