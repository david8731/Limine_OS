#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Initialise the framebuffer terminal. Returns false if no supported FB exists. */
bool console_init(void);
bool console_is_ready(void);

void console_write(const char *text);
void console_write_n(const char *text, size_t length);
void console_write_u64(uint64_t value);
void console_write_hex(uint64_t value); /* writes 0x followed by hexadecimal digits */

#endif
