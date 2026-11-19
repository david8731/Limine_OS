#ifndef ARCH_H
#define ARCH_H

#include <stdbool.h>

/*
 * Serial port (PL011 UART), implemented in src/arch/aarch64/uart.c.
 * In QEMU, "-serial stdio" connects it to the terminal that started QEMU.
 */
bool serial_init(void);       /* call after pmm_init() */
void serial_putc(char c);
int serial_getc(void);        /* -1 if no character is waiting */

#endif
