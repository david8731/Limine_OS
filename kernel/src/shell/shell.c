#include "shell.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "arch/arch.h"
#include "console/console.h"
#include "memory/pmm.h"

#define LINE_MAX 128

/* Write to the screen and to the serial port, so both show the same. */
static void shell_write(const char *text) {
    console_write(text);
    for (size_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\n') {
            serial_putc('\r');
        }
        serial_putc(text[i]);
    }
}

static void shell_write_u64(uint64_t value) {
    char buffer[21];
    size_t pos = sizeof(buffer) - 1;
    buffer[pos] = '\0';
    do {
        buffer[--pos] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0 && pos != 0);
    shell_write(&buffer[pos]);
}

static bool str_equal(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void command_help(void) {
    shell_write("Befehle:\n");
    shell_write("  help   diese Hilfe\n");
    shell_write("  mem    Speicher laut PMM\n");
    shell_write("  clear  Bildschirm leeren\n");
}

static void command_mem(void) {
    uint64_t total = pmm_total_pages();
    uint64_t free = pmm_free_pages_count();
    shell_write("Gesamt: ");
    shell_write_u64(total * 4);
    shell_write(" KiB (");
    shell_write_u64(total);
    shell_write(" Seiten)\nBelegt: ");
    shell_write_u64((total - free) * 4);
    shell_write(" KiB\nFrei:   ");
    shell_write_u64(free * 4);
    shell_write(" KiB\n");
}

static void run_command(const char *line) {
    if (line[0] == '\0') {
        return;
    } else if (str_equal(line, "help")) {
        command_help();
    } else if (str_equal(line, "mem")) {
        command_mem();
    } else if (str_equal(line, "clear")) {
        console_write("\033[2J\033[H");
    } else {
        shell_write("Unbekannter Befehl: ");
        shell_write(line);
        shell_write("\n(help zeigt alle Befehle)\n");
    }
}

void shell_run(void) {
    char line[LINE_MAX];
    size_t length = 0;

    shell_write("\nLimiTask Shell. Tippen im Terminal, in dem QEMU laeuft. 'help' fuer Befehle.\n> ");

    for (;;) {
        /* No interrupts yet, so simply ask the serial port again and again. */
        int c = serial_getc();
        if (c < 0) {
            continue;
        }

        if (c == '\r' || c == '\n') {
            shell_write("\n");
            line[length] = '\0';
            run_command(line);
            length = 0;
            shell_write("> ");
        } else if (c == 0x7F || c == '\b') {
            /* Backspace: remove the last character on screen and in the buffer. */
            if (length > 0) {
                length--;
                shell_write("\b \b");
            }
        } else if (c >= ' ' && c < 0x7F && length < LINE_MAX - 1) {
            char echo[2] = { (char)c, '\0' };
            line[length++] = (char)c;
            shell_write(echo);
        }
    }
}
