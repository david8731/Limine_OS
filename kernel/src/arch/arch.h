#ifndef ARCH_H
#define ARCH_H

#include <stdbool.h>

/*
 * Architecture interface. Each function is implemented once per architecture
 * in src/arch/<ARCH>/, and the build only compiles the directory of the
 * architecture being built.
 */

/* Install the exception/interrupt handlers (x86_64: GDT + IDT, aarch64: VBAR_EL1). */
void arch_init(void);

/* Trigger a breakpoint exception to check that the handlers work. */
void arch_breakpoint(void);

/* Disable interrupts and stop the CPU for good. */
__attribute__((noreturn)) void arch_halt(void);

/*
 * Serial port (x86_64: COM1, aarch64: PL011 UART). In QEMU it is connected
 * to the terminal that started QEMU when running with -serial stdio.
 * On aarch64 serial_init() needs the PMM, so call it after pmm_init().
 */
bool serial_init(void);
void serial_putc(char c);
int serial_getc(void); /* returns -1 if no character is waiting */

#endif
