#include "process.h"

#include <stddef.h>

#include "memory/pmm.h"

#define STACK_PAGES 4 /* 16 KiB stack per process */

static struct process table[MAX_PROCESSES];
static struct process *current;
static int next_pid;

/*
 * switch_context(&old->sp, new->sp): save the registers the C compiler
 * expects to survive a function call (x19-x30) on the old stack, remember
 * the old stack pointer, switch to the new stack and load its registers.
 * The final "ret" jumps to x30 of the new process, i.e. where it left off.
 */
void switch_context(uint64_t *old_sp, uint64_t new_sp);
__asm__ (
    ".global switch_context\n"
    "switch_context:\n"
    "    sub sp, sp, #96\n"
    "    stp x19, x20, [sp, #0]\n"
    "    stp x21, x22, [sp, #16]\n"
    "    stp x23, x24, [sp, #32]\n"
    "    stp x25, x26, [sp, #48]\n"
    "    stp x27, x28, [sp, #64]\n"
    "    stp x29, x30, [sp, #80]\n"
    "    mov x9, sp\n"
    "    str x9, [x0]\n"         /* *old_sp = sp */
    "    mov sp, x1\n"           /* sp = new_sp */
    "    ldp x19, x20, [sp, #0]\n"
    "    ldp x21, x22, [sp, #16]\n"
    "    ldp x23, x24, [sp, #32]\n"
    "    ldp x25, x26, [sp, #48]\n"
    "    ldp x27, x28, [sp, #64]\n"
    "    ldp x29, x30, [sp, #80]\n"
    "    add sp, sp, #96\n"
    "    ret\n"
);

/* Every new process starts here: run its function, then mark it finished. */
static void process_start(void) {
    current->entry();
    current->state = PROCESS_FINISHED;
    for (;;) {
        process_yield(); /* never chosen again, waits to be killed */
    }
}

static void copy_name(char *dst, const char *src) {
    int i = 0;
    while (src[i] != '\0' && i < PROCESS_NAME_MAX - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void process_init(void) {
    /* The code running right now becomes process 0, the shell. */
    current = &table[0];
    current->pid = next_pid++;
    copy_name(current->name, "shell");
    current->state = PROCESS_RUNNING;
}

int process_create(const char *name, void (*entry)(void)) {
    struct process *p = NULL;
    for (int i = 0; i < MAX_PROCESSES && p == NULL; i++) {
        if (table[i].state == PROCESS_FREE) {
            p = &table[i];
        }
    }
    if (p == NULL) {
        return -1;
    }

    uint64_t stack = pmm_alloc_pages(STACK_PAGES);
    if (stack == 0) {
        return -1;
    }

    /*
     * Prepare the new stack so that switch_context can "return" into it:
     * 12 saved registers, all 0, except x30 = process_start.
     */
    uint64_t *top = (uint64_t *)phys_to_virt(stack + STACK_PAGES * PAGE_SIZE);
    uint64_t *frame = top - 12;
    for (int i = 0; i < 12; i++) {
        frame[i] = 0;
    }
    frame[11] = (uint64_t)process_start; /* x30, where "ret" jumps to */

    p->pid = next_pid++;
    copy_name(p->name, name);
    p->entry = entry;
    p->sp = (uint64_t)frame;
    p->stack_phys = stack;
    p->steps = 0;
    p->state = PROCESS_READY;
    return p->pid;
}

void process_yield(void) {
    /* Round robin: the next READY entry after the current one. */
    int me = (int)(current - table);
    for (int k = 1; k <= MAX_PROCESSES; k++) {
        struct process *next = &table[(me + k) % MAX_PROCESSES];
        if (next->state != PROCESS_READY) {
            continue;
        }
        struct process *old = current;
        if (old->state == PROCESS_RUNNING) {
            old->state = PROCESS_READY;
        }
        next->state = PROCESS_RUNNING;
        next->steps++;
        current = next;
        switch_context(&old->sp, next->sp);
        return; /* we got the CPU back */
    }
}

bool process_kill(int pid) {
    for (int i = 1; i < MAX_PROCESSES; i++) { /* i = 0 is the shell */
        struct process *p = &table[i];
        if (p->state != PROCESS_FREE && p->pid == pid && p != current) {
            pmm_free_pages(p->stack_phys, STACK_PAGES);
            p->state = PROCESS_FREE;
            return true;
        }
    }
    return false;
}

const struct process *process_get(int i) {
    return &table[i];
}
