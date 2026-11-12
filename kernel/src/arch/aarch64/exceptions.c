#include "arch/arch.h"

#include <stdint.h>

#include "console/console.h"
#include "panic.h"

/*
 * Exception vectors. VBAR_EL1 points to a table of 16 entries of 128 bytes.
 * The CPU jumps straight into one of them, chosen by where the exception
 * came from (4 groups) and its kind (synchronous, IRQ, FIQ, SError):
 *   entries 0-3:   current EL, stack SP_EL0 (the kernel as Limine starts it)
 *   entries 4-7:   current EL, stack SP_EL1
 *   entries 8-11:  lower EL, 64 bit (user programs later)
 *   entries 12-15: lower EL, 32 bit
 * Every entry saves all registers and calls arm_exception().
 */

/* The registers as SAVE_AND_CALL leaves them on the stack. */
struct exception_frame {
    uint64_t x[31]; /* x0 to x30 */
    uint64_t elr;   /* address where the exception happened */
    uint64_t spsr;  /* saved processor state */
    uint64_t pad;   /* keeps the stack 16-byte aligned */
};

__asm__ (
    ".macro SAVE_AND_CALL type\n"
    "    sub sp, sp, #272\n"
    "    stp x0, x1, [sp, #0]\n"
    "    stp x2, x3, [sp, #16]\n"
    "    stp x4, x5, [sp, #32]\n"
    "    stp x6, x7, [sp, #48]\n"
    "    stp x8, x9, [sp, #64]\n"
    "    stp x10, x11, [sp, #80]\n"
    "    stp x12, x13, [sp, #96]\n"
    "    stp x14, x15, [sp, #112]\n"
    "    stp x16, x17, [sp, #128]\n"
    "    stp x18, x19, [sp, #144]\n"
    "    stp x20, x21, [sp, #160]\n"
    "    stp x22, x23, [sp, #176]\n"
    "    stp x24, x25, [sp, #192]\n"
    "    stp x26, x27, [sp, #208]\n"
    "    stp x28, x29, [sp, #224]\n"
    "    mrs x10, elr_el1\n"
    "    mrs x11, spsr_el1\n"
    "    str x30, [sp, #240]\n"
    "    stp x10, x11, [sp, #248]\n"
    "    mov x0, #\\type\n"   /* 1st argument: entry number (0-15) */
    "    mov x1, sp\n"        /* 2nd argument: struct exception_frame * */
    "    bl arm_exception\n"
    "    b exception_return\n"
    ".endm\n"
    "\n"
    ".pushsection .text.exception_vectors, \"ax\"\n"
    "exception_return:\n"
    "    ldp x10, x11, [sp, #248]\n"
    "    msr elr_el1, x10\n"
    "    msr spsr_el1, x11\n"
    "    ldr x30, [sp, #240]\n"
    "    ldp x0, x1, [sp, #0]\n"
    "    ldp x2, x3, [sp, #16]\n"
    "    ldp x4, x5, [sp, #32]\n"
    "    ldp x6, x7, [sp, #48]\n"
    "    ldp x8, x9, [sp, #64]\n"
    "    ldp x10, x11, [sp, #80]\n"
    "    ldp x12, x13, [sp, #96]\n"
    "    ldp x14, x15, [sp, #112]\n"
    "    ldp x16, x17, [sp, #128]\n"
    "    ldp x18, x19, [sp, #144]\n"
    "    ldp x20, x21, [sp, #160]\n"
    "    ldp x22, x23, [sp, #176]\n"
    "    ldp x24, x25, [sp, #192]\n"
    "    ldp x26, x27, [sp, #208]\n"
    "    ldp x28, x29, [sp, #224]\n"
    "    add sp, sp, #272\n"
    "    eret\n"              /* back to ELR_EL1 with the state from SPSR_EL1 */
    "\n"
    ".balign 2048\n"          /* VBAR_EL1 needs 2 KiB alignment */
    ".global exception_vectors\n"
    "exception_vectors:\n"
    ".irp i, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15\n"
    "    .balign 0x80\n"
    "    SAVE_AND_CALL \\i\n"
    ".endr\n"
    ".popsection\n"
);

extern char exception_vectors[];

static const char *exception_kind[4] = { "Synchronous", "IRQ", "FIQ", "SError" };

/* The exception class (bits 31:26 of ESR_EL1) says what happened. */
static const char *exception_class_name(uint64_t ec) {
    switch (ec) {
        case 0x00: return "unbekannter Befehl";
        case 0x15: return "SVC (Systemaufruf)";
        case 0x20: case 0x21: return "Instruction Abort (Code an ungueltiger Adresse)";
        case 0x22: return "PC nicht ausgerichtet";
        case 0x24: case 0x25: return "Data Abort (Zugriff auf ungueltige Adresse)";
        case 0x26: return "SP nicht ausgerichtet";
        case 0x3C: return "BRK (Breakpoint)";
        default:   return "anderer Grund";
    }
}

static void write_reg(const char *name, uint64_t value) {
    console_write(name);
    console_write("=");
    console_write_hex(value);
    console_write(" ");
}

/* Called from the assembly above, so not static. */
void arm_exception(uint64_t type, struct exception_frame *frame) {
    uint64_t esr, far;
    __asm__ volatile ("mrs %0, esr_el1" : "=r"(esr)); /* reason */
    __asm__ volatile ("mrs %0, far_el1" : "=r"(far)); /* faulting address for aborts */
    uint64_t ec = (esr >> 26) & 0x3F;

    /* A breakpoint (brk) is harmless and returns, handy for testing. */
    if (type % 4 == 0 && ec == 0x3C) {
        console_write("brk bei ");
        console_write_hex(frame->elr);
        console_write(" abgefangen, Exception-Vektoren funktionieren\n");
        frame->elr += 4; /* continue after the brk, every instruction is 4 bytes */
        return;
    }

    console_write("\nException: ");
    console_write(exception_kind[type % 4]);
    console_write(", ");
    console_write(exception_class_name(ec));
    console_write("\n");
    write_reg("ELR", frame->elr);
    write_reg("FAR", far);
    write_reg("ESR", esr);
    console_write("\n");
    write_reg("x0", frame->x[0]);
    write_reg("x1", frame->x[1]);
    write_reg("x30", frame->x[30]);
    console_write("\n");
    panic("unbehandelte CPU-Exception");
}

/*
 * On an exception the CPU always switches to the stack SP_EL1. Limine starts
 * the kernel on SP_EL0 and leaves SP_EL1 pointing at nothing, so give SP_EL1
 * a stack of its own.
 */
static uint8_t exception_stack[4096 * 4] __attribute__((aligned(16)));

void arch_init(void) {
    uint64_t spsel;
    __asm__ volatile ("mrs %0, spsel" : "=r"(spsel));
    if (spsel == 0) {
        __asm__ volatile (
            "msr spsel, #1\n"  /* switch to SP_EL1 for a moment */
            "mov sp, %0\n"     /* set the new stack */
            "msr spsel, #0\n"  /* back to SP_EL0 */
            : : "r"(exception_stack + sizeof(exception_stack)) : "memory");
    }
    __asm__ volatile ("msr vbar_el1, %0\n isb" : : "r"(exception_vectors));
}

void arch_breakpoint(void) {
    __asm__ volatile ("brk #0");
}

void arch_halt(void) {
    __asm__ volatile ("msr daifset, #0xf"); /* mask all interrupts */
    for (;;) {
        __asm__ volatile ("wfi");
    }
}
