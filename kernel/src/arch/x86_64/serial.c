#include "arch/arch.h"

#include "io.h"

#define COM1 0x3F8
#define LSR_DATA_READY  (1 << 0) /* line status: a character is waiting */
#define LSR_TX_EMPTY    (1 << 5) /* line status: transmit buffer is free */

static bool serial_ready;

bool serial_init(void) {
    outb(COM1 + 1, 0x00); /* interrupts off */
    outb(COM1 + 3, 0x80); /* DLAB on, to set the divisor */
    outb(COM1 + 0, 0x01); /* divisor 1 -> 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* 8 bits, no parity, 1 stop bit */
    outb(COM1 + 2, 0xC7); /* FIFO on and cleared */
    outb(COM1 + 4, 0x1E); /* loopback mode for a self test */
    outb(COM1 + 0, 0xAE);
    if (inb(COM1 + 0) != 0xAE) {
        return false;
    }
    outb(COM1 + 4, 0x0F); /* normal operation */
    serial_ready = true;
    return true;
}

void serial_putc(char c) {
    if (!serial_ready) {
        return;
    }
    while ((inb(COM1 + 5) & LSR_TX_EMPTY) == 0) {
        /* wait until the transmit buffer is free */
    }
    outb(COM1, (uint8_t)c);
}

int serial_getc(void) {
    if (!serial_ready || (inb(COM1 + 5) & LSR_DATA_READY) == 0) {
        return -1;
    }
    return inb(COM1);
}
