#include "shell.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "arch/arch.h"
#include "console/console.h"
#include "memory/pmm.h"
#include "process/process.h"

#define LINE_MAX 128

/*
 * Write to the screen and to the serial port, so both show the same.
 * Interrupts are off meanwhile: if the timer switched to another process
 * in the middle and that one wrote too, the console would get mixed up.
 */
static void shell_write(const char *text) {
    uint64_t irq_state = irq_disable();
    console_write(text);
    for (size_t i = 0; text[i] != '\0'; i++) {
        if (text[i] == '\n') {
            serial_putc('\r');
        }
        serial_putc(text[i]);
    }
    irq_restore(irq_state);
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
 * Programs that "start" can run as a process. None of them gives the CPU
 * away by itself: the timer interrupt takes it away every TICK_MS ms
 * (preemptive), so the shell and the others still get their turn.
 *
 * A line of output consists of several shell_write() calls. So that no
 * other process writes into the middle of it, interrupts are off for the
 * whole line: line_begin() ... line_end().
 */
static uint64_t line_begin(void) {
    return irq_disable();
}

static void line_end(uint64_t irq_state) {
    irq_restore(irq_state);
}

/* Runs forever and never gives the CPU away. Only the timer stops it. */
static void program_endless(void) {
    for (;;) {
    }
}

/*
 * Example calculation: adds 1 + 2 + ... + 1000000, then prints the result
 * and returns, which ends the process.
 */
static void program_sum(void) {
    volatile uint64_t sum = 0; /* volatile: really add, do not precompute */
    for (uint64_t n = 1; n <= 1000000; n++) {
        sum += n;
    }
    uint64_t line = line_begin();
    shell_write("\n[summe] 1 + 2 + ... + 1000000 = ");
    shell_write_u64(sum);
    shell_write("\n> ");
    line_end(line);
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
    uint64_t line = line_begin();
    write_tag();
    shell_write("fertig nach ");
    shell_write_u64(ms / 1000);
    shell_write(".");
    shell_write_u64((ms / 100) % 10);
    shell_write(" s, davon ");
    shell_write_u64(process_cpu_ms());
    shell_write(" ms Rechenzeit\n");
    line_end(line);
}

/*
 * Counts the prime numbers up to 3000000 by trial division and reports
 * every 10 %.
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
            uint64_t line = line_begin();
            write_tag();
            shell_write_u64(n / (PRIMES_LIMIT / 100));
            shell_write(" %, bisher ");
            shell_write_u64(count);
            shell_write(" Primzahlen\n");
            line_end(line);
        }
    }
    write_done();
}

/*
 * Counts down from 10 to 0, one number per second (measured with the timer).
 * The only program that gives the CPU away itself: while it waits there is
 * nothing to do, so it lets the others run instead of wasting its turn.
 * (FIFO and a high priority keep choosing it anyway.)
 */
static void program_countdown(void) {
    for (int i = 10; i >= 0; i--) {
        uint64_t line = line_begin();
        write_tag();
        shell_write_u64((uint64_t)i);
        shell_write("\n");
        line_end(line);
        uint64_t until = timer_ms() + 1000;
        while (i > 0 && timer_ms() < until) {
            process_yield();
        }
    }
    write_done();
}

/*
 * Draws a progress bar in 10 steps: "[A] ####...... 40 %".
 * Every step needs a tenth of total_ms CPU time (counted by the kernel, see
 * process_cpu_ms), so a bar alone takes total_ms. With several bars you can
 * see in which order the scheduler gives them the CPU.
 */
/* How far each bar is, by table slot. bar_pid says which process it was. */
static int bar_step[MAX_PROCESSES];
static int bar_pid[MAX_PROCESSES];

/*
 * True if this bar is the last of several running bars to reach step,
 * i.e. a round is complete. Then a blank line separates the rounds.
 */
static bool bar_round_complete(int slot, int step) {
    int others = 0;
    for (int i = 1; i < MAX_PROCESSES; i++) { /* 0 is the shell */
        const struct process *p = process_get(i);
        bool running = p->state == PROCESS_READY || p->state == PROCESS_RUNNING;
        bool is_bar = bar_pid[i] == p->pid; /* a bar that has begun */
        if (i != slot && running && is_bar) {
            if (bar_step[i] < step) {
                return false;
            }
            others++;
        }
    }
    return others > 0;
}

static void run_bar(uint64_t total_ms) {
    int slot = (int)(process_current() - process_get(0));
    bar_pid[slot] = process_current()->pid;
    bar_step[slot] = 0;
    for (int step = 1; step <= 10; step++) {
        while (process_cpu_ms() < step * total_ms / 10) {
            /* busy: this is the "work", until the timer interrupts us */
        }
        uint64_t line = line_begin();
        write_tag();
        for (int i = 1; i <= 10; i++) {
            shell_write(i <= step ? "#" : ".");
        }
        shell_write(" ");
        shell_write_u64((uint64_t)step * 10);
        shell_write(" %\n");
        bar_step[slot] = step;
        if (bar_round_complete(slot, step)) {
            shell_write("\n");
        }
        line_end(line);
    }
    write_done();
}

static void program_bar(void)       { run_bar(3000); } /* balken, demo */
static void program_bar_long(void)  { run_bar(6000); } /* demo2 */
static void program_bar_short(void) { run_bar(1000); } /* demo2 */

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
    shell_write("\nPID  ZUSTAND  PRIO  SCHRITTE  CPU-MS    NAME\n");
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
        shell_write_u64_padded(p->cpu_ms, 10);
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

/*
 * Three bars of different length, started longest first:
 * lang 6 s (Prio 1), mittel 3 s (Prio 2), kurz 1 s (Prio 3).
 * FIFO: the short one waits for the long one (convoy effect).
 * Round Robin: the short one is done early without anyone knowing its length.
 * Priority: shortest first ("Shortest Job First"), the fastest on average.
 */
static void command_demo2(void) {
    process_create("lang", program_bar_long, 1);
    process_create("mittel", program_bar, 2);
    process_create("kurz", program_bar_short, 3);
    shell_write("Balken lang 6 s (Prio 1), mittel 3 s (Prio 2), kurz 1 s (Prio 3) gestartet, Scheduler: ");
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
    shell_write("  demo                  drei gleiche Balken mit Prio 1, 2, 3 starten\n");
    shell_write("  demo2                 Balken lang, mittel, kurz mit Prio 1, 2, 3\n");
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
    } else if (str_equal(line, "demo2")) {
        command_demo2();
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
