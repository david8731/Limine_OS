#include "arch/arch.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/aarch64/mmio.h"

/* PL011 UART of the QEMU "virt" machine. */
#define UART_PHYS    0x09000000
#define UART_DR      0x00       /* data register */
#define UART_FR      0x18       /* flag register */
#define UART_FR_RXFE (1 << 4)   /* receive FIFO empty */
#define UART_FR_TXFF (1 << 5)   /* transmit FIFO full */

static volatile uint32_t *uart;

bool serial_init(void) {
    /* QEMU has already set up the UART, it only has to be reachable. */
    uart = map_device_page(UART_PHYS);
    return uart != NULL;
}

void serial_putc(char c) {
    if (uart == NULL) {
        return;
    }
    while (uart[UART_FR / 4] & UART_FR_TXFF) {
        /* wait for room in the transmit FIFO */
    }
    uart[UART_DR / 4] = (uint8_t)c;
}

int serial_getc(void) {
    if (uart == NULL || (uart[UART_FR / 4] & UART_FR_RXFE)) {
        return -1;
    }
    return (int)(uart[UART_DR / 4] & 0xFF);
}
