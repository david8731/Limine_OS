#ifndef ARCH_H
#define ARCH_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Serial port (PL011 UART), implemented in src/arch/aarch64/uart.c.
 * In QEMU, "-serial stdio" connects it to the terminal that started QEMU.
 */
bool serial_init(void);       /* call after pmm_init() */
void serial_putc(char c);
int serial_getc(void);        /* -1 if no character is waiting */

/* Milliseconds since boot (Generic Timer), in src/arch/aarch64/timer.c. */
uint64_t timer_ms(void);

/*
 * Interrupts, in src/arch/aarch64/interrupts.c: installs the vector table,
 * sets up the interrupt controller (GIC) and a timer interrupt every
 * TICK_MS milliseconds. Every tick takes the CPU away from the running
 * process (process_yield). Call after process_init().
 */
#define TICK_MS 10
bool interrupts_init(void);

/*
 * Switch interrupts off and return the old state, so that a piece of code
 * cannot be interrupted in the middle. irq_restore() puts the old state
 * back. (DAIF is the register with the interrupt mask bits, I = IRQs.)
 */
static inline uint64_t irq_disable(void) {
    uint64_t daif;
    __asm__ volatile ("mrs %0, daif\n msr daifset, #2" : "=r"(daif) : : "memory");
    return daif;
}

static inline void irq_restore(uint64_t daif) {
    __asm__ volatile ("msr daif, %0" : : "r"(daif) : "memory");
}

static inline void irq_enable(void) {
    __asm__ volatile ("msr daifclr, #2" : : : "memory");
}

#endif
