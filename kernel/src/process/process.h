#ifndef PROCESS_H
#define PROCESS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Processes, as simple as possible:
 * - a fixed table of MAX_PROCESSES entries, no heap needed
 * - every process has its own stack (pages from the PMM)
 * - preemptive: every TICK_MS ms the timer interrupt calls process_yield()
 *   and takes the CPU away. A process may also give it away itself earlier.
 * Process 0 is the shell, i.e. the code that called process_init().
 * The shell gets a turn after every other process, whatever the scheduler,
 * so that typing always works. The scheduler only decides which of the
 * other processes runs next.
 */

#define MAX_PROCESSES 16
#define PROCESS_NAME_MAX 16

enum process_state {
    PROCESS_FREE,     /* slot unused */
    PROCESS_READY,    /* waiting for its turn */
    PROCESS_RUNNING,  /* running right now */
    PROCESS_FINISHED, /* its function returned, waits for kill */
};

enum scheduler {
    SCHED_ROUND_ROBIN, /* everyone in turn */
    SCHED_PRIORITY,    /* highest priority first, equal ones in turn */
    SCHED_FIFO,        /* oldest process until it is finished */
};

struct process {
    int pid;
    char name[PROCESS_NAME_MAX];
    enum process_state state;
    void (*entry)(void);  /* function the process runs */
    uint64_t sp;          /* saved stack pointer while not running */
    uint64_t stack_phys;  /* stack pages from the PMM, 0 for the shell */
    uint64_t steps;       /* how often it got the CPU */
    int priority;         /* only used by SCHED_PRIORITY, higher = first */
    uint64_t start_ms;    /* when it was started (timer_ms) */
    uint64_t cpu_ms;      /* CPU time used so far, without the current turn */
    uint64_t running_since; /* when its current turn began (timer_ms) */
};

void process_init(void);

/*
 * Start a new process that runs entry(). Returns its pid, or -1 if that fails.
 * A finished process is cleaned up automatically when its slot is needed.
 */
int process_create(const char *name, void (*entry)(void), int priority);

/*
 * Give the CPU to the next ready process. Returns when it is our turn again.
 * The timer interrupt calls it, a process can also call it itself.
 */
void process_yield(void);

/* Remove a process and free its stack. The shell (pid 0) cannot be killed. */
bool process_kill(int pid);

void process_set_scheduler(enum scheduler s);
enum scheduler process_get_scheduler(void);

/* CPU time of the running process so far, including its current turn. */
uint64_t process_cpu_ms(void);

/* The process that is running right now. */
const struct process *process_current(void);

/* Entry i of the process table, for ps. */
const struct process *process_get(int i);

#endif
