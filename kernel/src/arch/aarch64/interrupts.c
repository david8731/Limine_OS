#include "arch/arch.h"

#include "arch/aarch64/mmio.h"
#include "console/console.h"
#include "process/process.h"

/*
 * GICv2, the interrupt controller of the QEMU "virt" machine. It has two
 * parts: the distributor (GICD) decides which interrupts are switched on,
 * the CPU interface (GICC) hands them to the CPU one by one.
 */
#define GICD_PHYS      0x08000000
#define GICC_PHYS      0x08010000
#define GICD_CTLR      0x000  /* distributor on/off */
#define GICD_ISENABLER 0x100  /* one bit per interrupt: switch it on */
#define GICC_CTLR      0x000  /* CPU interface on/off */
#define GICC_PMR       0x004  /* priority mask: let everything through */
#define GICC_IAR       0x00C  /* read: which interrupt is it (acknowledge) */
#define GICC_EOIR      0x010  /* write: this interrupt is handled */
#define SPURIOUS_IRQ   1023   /* "nothing there" */

/* Interrupt number of the virtual timer of the Generic Timer. */
#define TIMER_IRQ 27

extern char exception_vectors[]; /* in vectors.S */

static volatile uint32_t *gicd;
static volatile uint32_t *gicc;

/* Let the timer fire again in TICK_MS milliseconds. */
static void timer_rearm(void) {
    uint64_t freq;
    __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(freq));
    __asm__ volatile ("msr cntv_tval_el0, %0" : : "r"(freq / 1000 * TICK_MS));
}

bool interrupts_init(void) {
    /* 1. Tell the CPU where the vector table is. */
    __asm__ volatile ("msr vbar_el1, %0\n isb" : : "r"(exception_vectors));

    /* 2. Interrupt controller: switch on the timer interrupt. */
    gicd = map_device_page(GICD_PHYS);
    gicc = map_device_page(GICC_PHYS);
    if (gicd == NULL || gicc == NULL) {
        return false;
    }
    gicd[GICD_ISENABLER / 4] = 1u << TIMER_IRQ;
    gicd[GICD_CTLR / 4] = 1;
    gicc[GICC_PMR / 4] = 0xFF;
    gicc[GICC_CTLR / 4] = 1;

    /* 3. Timer: first tick in TICK_MS ms (CNTV_CTL: bit 0 = enable). */
    timer_rearm();
    __asm__ volatile ("msr cntv_ctl_el0, %0" : : "r"((uint64_t)1));

    /* 4. From now on the CPU accepts interrupts. */
    irq_enable();
    return true;
}

/*
 * Called by irq_entry in vectors.S, with interrupts switched off.
 * For a timer tick: arm the next tick, then take the CPU away from the
 * running process. process_yield() returns only when this process gets
 * its turn again, then irq_entry continues it where it was interrupted.
 */
void irq_handler(void) {
    uint32_t iar = gicc[GICC_IAR / 4];
    uint32_t irq = iar & 0x3FF;
    if (irq == SPURIOUS_IRQ) {
        return;
    }
    if (irq == TIMER_IRQ) {
        timer_rearm(); /* first, otherwise the timer keeps signalling */
    }
    gicc[GICC_EOIR / 4] = iar; /* tell the GIC we are done with it */
    if (irq == TIMER_IRQ) {
        process_yield();
    }
}

/* Print text on the screen and on the serial port. */
static void write_both(const char *text) {
    console_write(text);
    for (; *text != '\0'; text++) {
        if (*text == '\n') {
            serial_putc('\r');
        }
        serial_putc(*text);
    }
}

static void write_hex(uint64_t value) {
    char buffer[19] = "0x";
    for (int i = 0; i < 16; i++) {
        buffer[2 + i] = "0123456789abcdef"[(value >> (60 - i * 4)) & 0xF];
    }
    buffer[18] = '\0';
    write_both(buffer);
}

/*
 * Called by vectors.S for everything that is not an interrupt, i.e. errors
 * such as a bad memory access. ESR says why, ELR at which instruction,
 * FAR at which address. The kernel cannot fix that, so it stops.
 */
void exception_handler(uint64_t entry) {
    uint64_t esr, elr, far;
    __asm__ volatile ("mrs %0, esr_el1" : "=r"(esr));
    __asm__ volatile ("mrs %0, elr_el1" : "=r"(elr));
    __asm__ volatile ("mrs %0, far_el1" : "=r"(far));

    write_both("\n*** CPU-Ausnahme, Eintrag ");
    write_hex(entry);
    write_both("\nESR ");
    write_hex(esr);
    write_both(" (Grund, Bits 26-31: ");
    write_hex((esr >> 26) & 0x3F);
    write_both(")\nELR ");
    write_hex(elr);
    write_both(" (Befehl)\nFAR ");
    write_hex(far);
    write_both(" (Adresse)\nKernel angehalten.\n");
    for (;;) {
        __asm__ volatile ("wfi");
    }
}
