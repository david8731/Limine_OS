#ifndef PROCESS_H
#define PROCESS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Processes, as simple as possible:
 * - a fixed table of MAX_PROCESSES entries, no heap needed
 * - every process has its own stack (pages from the PMM)
 * - cooperative: a process runs until it calls process_yield() itself
 * Process 0 is the shell, i.e. the code that called process_init().
 */

#define MAX_PROCESSES 16
#define PROCESS_NAME_MAX 16

enum process_state {
    PROCESS_FREE,     /* slot unused */
    PROCESS_READY,    /* waiting for its turn */
    PROCESS_RUNNING,  /* running right now */
    PROCESS_FINISHED, /* its function returned, waits for kill */
};

struct process {
    int pid;
    char name[PROCESS_NAME_MAX];
    enum process_state state;
    void (*entry)(void);  /* function the process runs */
    uint64_t sp;          /* saved stack pointer while not running */
    uint64_t stack_phys;  /* stack pages from the PMM, 0 for the shell */
    uint64_t steps;       /* how often it got the CPU */
};

void process_init(void);

/* Start a new process that runs entry(). Returns its pid, or -1 if that fails. */
int process_create(const char *name, void (*entry)(void));

/* Give the CPU to the next ready process. Returns when it is our turn again. */
void process_yield(void);

/* Remove a process and free its stack. The shell (pid 0) cannot be killed. */
bool process_kill(int pid);

/* Entry i of the process table, for ps. */
const struct process *process_get(int i);

#endif
