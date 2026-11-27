#include "arch/arch.h"

/*
 * The ARM Generic Timer counts up all the time, cntfrq_el0 times per second.
 * Reading it needs no setup and no interrupts.
 */
uint64_t timer_ms(void) {
    uint64_t count, freq;
    __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(count));
    __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(freq));
    return count / (freq / 1000);
}
