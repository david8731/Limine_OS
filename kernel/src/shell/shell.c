#include "shell.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "arch/arch.h"
#include "console/console.h"
#include "memory/pmm.h"
#include "process/process.h"

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

/* Number, then spaces until it is width characters wide (for tables). */
static void shell_write_u64_padded(uint64_t value, int width) {
    int digits = 1;
    for (uint64_t v = value; v >= 10; v /= 10) {
        digits++;
    }
    shell_write_u64(value);
    for (; digits < width; digits++) {
        shell_write(" ");
    }
}

static bool str_equal(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* If line starts with prefix, return the rest of the line, otherwise NULL. */
static const char *after_prefix(const char *line, const char *prefix) {
    while (*prefix != '\0') {
        if (*line++ != *prefix++) {
            return NULL;
        }
    }
    return line;
}

/*
 * Programs that "start" can run as a process. Each one does a little work
 * and then calls process_yield(), so that the shell and the others get the
 * CPU again. Without that the shell would hang.
 */
static void program_endless(void) {
    for (;;) {
        process_yield();
    }
}

/*
 * Example calculation: adds 1 + 2 + ... + 1000000. After every 1000 numbers
 * it gives the CPU away, so the shell stays usable while it calculates.
 * When it is done it prints the result and returns, which ends the process.
 */
static void program_sum(void) {
    uint64_t sum = 0;
    for (uint64_t n = 1; n <= 1000000; n++) {
        sum += n;
        if (n % 1000 == 0) {
            process_yield();
        }
    }
    shell_write("\n[summe] 1 + 2 + ... + 1000000 = ");
    shell_write_u64(sum);
    shell_write("\n> ");
}

static const char *state_name(enum process_state state) {
    switch (state) {
        case PROCESS_READY:    return "bereit ";
        case PROCESS_RUNNING:  return "laeuft ";
        case PROCESS_FINISHED: return "beendet";
        default:               return "?      ";
    }
}

static void command_ps(void) {
    shell_write("PID  ZUSTAND  SCHRITTE  NAME\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        const struct process *p = process_get(i);
        if (p->state == PROCESS_FREE) {
            continue;
        }
        shell_write_u64_padded((uint64_t)p->pid, 5);
        shell_write(state_name(p->state));
        shell_write("  ");
        shell_write_u64_padded(p->steps, 10);
        shell_write(p->name);
        shell_write("\n");
    }
}

static void command_start(const char *name) {
    void (*entry)(void) = NULL;
    if (str_equal(name, "endlos")) {
        entry = program_endless;
    } else if (str_equal(name, "summe")) {
        entry = program_sum;
    } else {
        shell_write("Unbekanntes Programm. Moeglich: endlos, summe\n");
        return;
    }

    int pid = process_create(name, entry);
    if (pid < 0) {
        shell_write("Kein Platz fuer einen neuen Prozess\n");
        return;
    }
    shell_write("Gestartet mit PID ");
    shell_write_u64((uint64_t)pid);
    shell_write("\n");
}

static void command_kill(const char *arg) {
    int pid = 0;
    if (*arg == '\0') {
        shell_write("Bitte eine PID angeben, z. B. kill 1\n");
        return;
    }
    for (; *arg != '\0'; arg++) {
        if (*arg < '0' || *arg > '9') {
            shell_write("Das ist keine Zahl\n");
            return;
        }
        pid = pid * 10 + (*arg - '0');
    }
    if (process_kill(pid)) {
        shell_write("Prozess beendet\n");
    } else {
        shell_write("Keinen solchen Prozess gefunden (die Shell, PID 0, geht nicht)\n");
    }
}

static void command_help(void) {
    shell_write("Befehle:\n");
    shell_write("  help          diese Hilfe\n");
    shell_write("  mem           Speicher laut PMM\n");
    shell_write("  clear         Bildschirm leeren\n");
    shell_write("  ps            alle Prozesse anzeigen\n");
    shell_write("  start <name>  Prozess starten (endlos oder summe)\n");
    shell_write("  kill <pid>    Prozess beenden\n");
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
    const char *arg;
    if (line[0] == '\0') {
        return;
    } else if (str_equal(line, "help")) {
        command_help();
    } else if (str_equal(line, "mem")) {
        command_mem();
    } else if (str_equal(line, "clear")) {
        console_write("\033[2J\033[H");
    } else if (str_equal(line, "ps")) {
        command_ps();
    } else if ((arg = after_prefix(line, "start ")) != NULL) {
        command_start(arg);
    } else if ((arg = after_prefix(line, "kill ")) != NULL) {
        command_kill(arg);
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
        /* Nothing typed yet: let the other processes run meanwhile. */
        int c = serial_getc();
        if (c < 0) {
            process_yield();
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
