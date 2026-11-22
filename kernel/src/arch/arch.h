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

#endif
