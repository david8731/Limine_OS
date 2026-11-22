/*
 * The exception vector table. VBAR_EL1 points here (set in interrupts.c).
 * When an exception or interrupt happens, the CPU jumps to one of the
 * 16 entries, each 128 bytes long:
 *
 *   offset  from where                     Sync   IRQ    FIQ    SError
 *   0x000   current EL, stack pointer SP0  0      1      2      3
 *   0x200   current EL, stack pointer SPx  4      5      6      7
 *   0x400   lower EL (user), aarch64       8      9      10     11
 *   0x600   lower EL (user), aarch32       12     13     14     15
 *
 * Our kernel runs in EL1 with SP0, so only the first two rows can happen
 * (in practice the first row, SP0 means SP_EL0). The IRQ
 * entries (1 and 5) go to irq_entry, all others are errors: they call
 * exception_handler(entry number), which prints what happened and halts.
 */

/*
 * On an exception the CPU switches to the stack pointer SP_EL1, which Limine
 * never set up for us. "msr spsel, #0" goes back to SP_EL0, the stack of the
 * interrupted process. All our code, processes and handlers, uses SP_EL0.
 * eret later restores SPSel from SPSR_EL1 anyway.
 */
.macro error_entry number
    .balign 128
    msr spsel, #0
    mov x0, #\number
    b exception_handler
.endm

.macro irq_vector
    .balign 128
    msr spsel, #0
    b irq_entry
.endm

.section .text
.balign 2048                  /* VBAR_EL1 must be 2 KiB aligned */
.global exception_vectors
exception_vectors:
    error_entry 0
    irq_vector
    error_entry 2
    error_entry 3
    error_entry 4
    irq_vector
    error_entry 6
    error_entry 7
    error_entry 8
    error_entry 9
    error_entry 10
    error_entry 11
    error_entry 12
    error_entry 13
    error_entry 14
    error_entry 15

/*
 * An interrupt can come between any two instructions, so the interrupted
 * code must not notice anything: save ALL registers on its stack, call the
 * C handler, load them back and continue with "eret".
 * ELR_EL1 = address to continue at, SPSR_EL1 = saved PSTATE (flags, mask).
 * Both have to be saved too, because irq_handler may switch to another
 * process, which can be interrupted itself and overwrite them.
 * 31 registers + ELR + SPSR = 33 values, 34 * 8 = 272 bytes (16-aligned).
 */
irq_entry:
    sub sp, sp, #272
    stp x0,  x1,  [sp, #0]
    stp x2,  x3,  [sp, #16]
    stp x4,  x5,  [sp, #32]
    stp x6,  x7,  [sp, #48]
    stp x8,  x9,  [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x30, x0,  [sp, #240]
    str x1,       [sp, #256]

    bl irq_handler            /* may switch processes, returns later */

    ldr x1,       [sp, #256]
    ldp x30, x0,  [sp, #240]
    msr elr_el1, x0
    msr spsr_el1, x1
    ldp x0,  x1,  [sp, #0]
    ldp x2,  x3,  [sp, #16]
    ldp x4,  x5,  [sp, #32]
    ldp x6,  x7,  [sp, #48]
    ldp x8,  x9,  [sp, #64]
    ldp x10, x11, [sp, #80]
    ldp x12, x13, [sp, #96]
    ldp x14, x15, [sp, #112]
    ldp x16, x17, [sp, #128]
    ldp x18, x19, [sp, #144]
    ldp x20, x21, [sp, #160]
    ldp x22, x23, [sp, #176]
    ldp x24, x25, [sp, #192]
    ldp x26, x27, [sp, #208]
    ldp x28, x29, [sp, #224]
    add sp, sp, #272
    eret                      /* back to where the interrupt came */
