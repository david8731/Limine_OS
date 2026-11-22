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

/* Prints "[name] " at the start of every line a program writes. */
static void write_tag(void) {
    shell_write("[");
    shell_write(process_current()->name);
    shell_write("] ");
}

/*
 * Prints "fertig nach 3.4 s": the time since the process was started,
 * including the time it waited for the CPU.
 */
static void write_done(void) {
    uint64_t ms = timer_ms() - process_current()->start_ms;
    write_tag();
    shell_write("fertig nach ");
    shell_write_u64(ms / 1000);
    shell_write(".");
    shell_write_u64((ms / 100) % 10);
    shell_write(" s\n");
}

/*
 * Counts the prime numbers up to 3000000 by trial division and reports
 * every 10 %. It gives the CPU away after every 100 numbers it checks.
 */
#define PRIMES_LIMIT 3000000
static void program_primes(void) {
    uint64_t count = 0;
    for (uint64_t n = 2; n <= PRIMES_LIMIT; n++) {
        bool prime = true;
        for (uint64_t d = 2; d * d <= n && prime; d++) {
            prime = (n % d) != 0;
        }
        if (prime) {
            count++;
        }
        if (n % (PRIMES_LIMIT / 10) == 0) {
            write_tag();
            shell_write_u64(n / (PRIMES_LIMIT / 100));
            shell_write(" %, bisher ");
            shell_write_u64(count);
            shell_write(" Primzahlen\n");
        }
        if (n % 100 == 0) {
            process_yield();
        }
    }
    write_done();
}

/*
 * Counts down from 10 to 0, one number per second (measured with the timer).
 * While it waits it gives the CPU away, so it does not slow down the others,
 * unless the scheduler keeps it on the CPU (FIFO, high priority).
 */
static void program_countdown(void) {
    for (int i = 10; i >= 0; i--) {
        write_tag();
        shell_write_u64((uint64_t)i);
        shell_write("\n");
        uint64_t until = timer_ms() + 1000;
        while (i > 0 && timer_ms() < until) {
            process_yield();
        }
    }
    write_done();
}

/*
 * Draws a progress bar in 10 steps: "[A] ####...... 40 %".
 * Every step needs 300 ms of CPU time, in slices of 5 ms: the process
 * keeps the CPU busy for 5 ms, then gives it away. So a bar alone takes
 * 3 s, and with several bars you can see in which order they get the CPU.
 */
#define BAR_SLICE_MS 5
#define BAR_SLICES_PER_STEP 60
static void program_bar(void) {
    for (int step = 1; step <= 10; step++) {
        for (int slice = 0; slice < BAR_SLICES_PER_STEP; slice++) {
            uint64_t until = timer_ms() + BAR_SLICE_MS;
            while (timer_ms() < until) {
                /* busy: this is the "work" */
            }
            process_yield();
        }
        write_tag();
        for (int i = 1; i <= 10; i++) {
            shell_write(i <= step ? "#" : ".");
        }
        shell_write(" ");
        shell_write_u64((uint64_t)step * 10);
        shell_write(" %\n");
    }
    write_done();
}

static const char *state_name(enum process_state state) {
    switch (state) {
        case PROCESS_READY:    return "bereit ";
        case PROCESS_RUNNING:  return "laeuft ";
        case PROCESS_FINISHED: return "beendet";
        default:               return "?      ";
    }
}

static const char *scheduler_name(enum scheduler s) {
    switch (s) {
        case SCHED_PRIORITY: return "Prioritaet";
        case SCHED_FIFO:     return "FIFO";
        default:             return "Round Robin";
    }
}

static void command_ps(void) {
    shell_write("Scheduler: ");
    shell_write(scheduler_name(process_get_scheduler()));
    shell_write("\nPID  ZUSTAND  PRIO  SCHRITTE  NAME\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        const struct process *p = process_get(i);
        if (p->state == PROCESS_FREE) {
            continue;
        }
        shell_write_u64_padded((uint64_t)p->pid, 5);
        shell_write(state_name(p->state));
        shell_write("  ");
        if (p->pid == 0) {
            shell_write("-     ");
        } else {
            shell_write_u64_padded((uint64_t)p->priority, 6);
        }
        shell_write_u64_padded(p->steps, 10);
        shell_write(p->name);
        shell_write("\n");
    }
}

/* Read a number from text. Returns -1 if text is not a number. */
static int parse_number(const char *text) {
    int value = 0;
    if (*text == '\0') {
        return -1;
    }
    for (; *text != '\0'; text++) {
        if (*text < '0' || *text > '9' || value > 100000) {
            return -1;
        }
        value = value * 10 + (*text - '0');
    }
    return value;
}

/* "start <name>" or "start <name> <priority>" */
static void command_start(const char *arg) {
    char name[PROCESS_NAME_MAX];
    int len = 0;
    while (arg[len] != '\0' && arg[len] != ' ' && len < PROCESS_NAME_MAX - 1) {
        name[len] = arg[len];
        len++;
    }
    name[len] = '\0';

    int priority = 1;
    if (arg[len] == ' ') {
        priority = parse_number(&arg[len + 1]);
        if (priority < 0) {
            shell_write("Die Prioritaet muss eine Zahl sein, z. B. start summe 5\n");
            return;
        }
    }

    void (*entry)(void) = NULL;
    if (str_equal(name, "endlos")) {
        entry = program_endless;
    } else if (str_equal(name, "summe")) {
        entry = program_sum;
    } else if (str_equal(name, "primzahlen")) {
        entry = program_primes;
    } else if (str_equal(name, "countdown")) {
        entry = program_countdown;
    } else if (str_equal(name, "balken")) {
        entry = program_bar;
    } else {
        shell_write("Unbekanntes Programm. Moeglich: endlos, summe, primzahlen, countdown, balken\n");
        return;
    }

    int pid = process_create(name, entry, priority);
    if (pid < 0) {
        shell_write("Kein Platz fuer einen neuen Prozess\n");
        return;
    }
    shell_write("Gestartet mit PID ");
    shell_write_u64((uint64_t)pid);
    shell_write("\n");
}

/*
 * Starts three progress bars A, B and C with priority 1, 2 and 3.
 * Round Robin: they take turns. FIFO: A, then B, then C.
 * Priority: C first, then B, then A. Choose the scheduler before the demo.
 */
static void command_demo(void) {
    process_create("A", program_bar, 1);
    process_create("B", program_bar, 2);
    process_create("C", program_bar, 3);
    shell_write("Balken A (Prio 1), B (Prio 2) und C (Prio 3) gestartet, Scheduler: ");
    shell_write(scheduler_name(process_get_scheduler()));
    shell_write("\n");
}

static void command_sched(const char *arg) {
    if (str_equal(arg, "rr")) {
        process_set_scheduler(SCHED_ROUND_ROBIN);
    } else if (str_equal(arg, "prio")) {
        process_set_scheduler(SCHED_PRIORITY);
    } else if (str_equal(arg, "fifo")) {
        process_set_scheduler(SCHED_FIFO);
    } else if (*arg != '\0') {
        shell_write("Moeglich: sched rr, sched prio, sched fifo\n");
        return;
    }
    shell_write("Scheduler: ");
    shell_write(scheduler_name(process_get_scheduler()));
    shell_write("\n");
}

static void command_kill(const char *arg) {
    int pid = parse_number(arg);
    if (pid < 0) {
        shell_write("Bitte eine PID angeben, z. B. kill 1\n");
        return;
    }
    if (process_kill(pid)) {
        shell_write("Prozess beendet\n");
    } else {
        shell_write("Keinen solchen Prozess gefunden (die Shell, PID 0, geht nicht)\n");
    }
}

static void command_help(void) {
    shell_write("Befehle:\n");
    shell_write("  help                  diese Hilfe\n");
    shell_write("  mem                   Speicher laut PMM\n");
    shell_write("  clear                 Bildschirm leeren\n");
    shell_write("  ps                    alle Prozesse anzeigen\n");
    shell_write("  start <name> [prio]   Prozess starten: endlos, summe, primzahlen,\n");
    shell_write("                        countdown, balken\n");
    shell_write("  demo                  drei Balken mit Prio 1, 2, 3 starten\n");
    shell_write("  sched [rr|prio|fifo]  Scheduler anzeigen oder wechseln\n");
    shell_write("  kill <pid>            Prozess beenden\n");
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
    } else if (str_equal(line, "demo")) {
        command_demo();
    } else if (str_equal(line, "sched")) {
        command_sched("");
    } else if ((arg = after_prefix(line, "sched ")) != NULL) {
        command_sched(arg);
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
