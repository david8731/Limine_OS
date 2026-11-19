#include "arch/arch.h"

#include <stddef.h>
#include <stdint.h>

#include "mmio.h"

/* PL011 UART of the QEMU "virt" machine, at a fixed physical address. */
#define UART_PHYS    0x09000000
#define UART_DR      0x00 /* data register */
#define UART_FR      0x18 /* flag register */
#define UART_IBRD    0x24 /* baud rate divisor, integer part */
#define UART_FBRD    0x28 /* baud rate divisor, fractional part */
#define UART_LCRH    0x2C /* line control */
#define UART_CR      0x30 /* control register */
#define UART_FR_RXFE (1 << 4) /* receive FIFO empty */
#define UART_FR_TXFF (1 << 5) /* transmit FIFO full */

static volatile uint32_t *uart;

static void uart_write(uint32_t reg, uint32_t value) {
    uart[reg / 4] = value;
}

static uint32_t uart_read(uint32_t reg) {
    return uart[reg / 4];
}

bool serial_init(void) {
    uart = map_device_page(UART_PHYS);
    if (uart == NULL) {
        return false;
    }

    uart_write(UART_CR, 0);                              /* off while configuring */
    uart_write(UART_IBRD, 13);                           /* 24 MHz / (16 * 115200) = 13.02 */
    uart_write(UART_FBRD, 1);
    uart_write(UART_LCRH, (3 << 5) | (1 << 4));          /* 8 bits, FIFO on */
    uart_write(UART_CR, (1 << 0) | (1 << 8) | (1 << 9)); /* UART, transmit, receive on */
    return true;
}

void serial_putc(char c) {
    if (uart == NULL) {
        return;
    }
    while (uart_read(UART_FR) & UART_FR_TXFF) {
        /* wait for room in the transmit FIFO */
    }
    uart_write(UART_DR, (uint8_t)c);
}

int serial_getc(void) {
    if (uart == NULL || (uart_read(UART_FR) & UART_FR_RXFE)) {
        return -1;
    }
    return (int)(uart_read(UART_DR) & 0xFF);
}
