#include "arch/arch.h"

#include <stdint.h>

#include "gdt.h"
#include "io.h"
#include "console/console.h"
#include "panic.h"

/*
 * IDT with handlers for the 32 CPU exceptions. __attribute__((interrupt))
 * makes the compiler save the registers and return with iretq.
 */

struct interrupt_frame {
    uint64_t rip, cs, rflags, rsp, ss;
};

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct descriptor_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[256];

static const char *exception_names[32] = {
    "Division Error", "Debug", "NMI", "Breakpoint", "Overflow",
    "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS",
    "Segment Not Present", "Stack-Segment Fault", "General Protection Fault",
    "Page Fault", "Reserved", "x87 Floating-Point", "Alignment Check",
    "Machine Check", "SIMD Floating-Point", "Virtualization",
    "Control Protection", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Hypervisor Injection", "VMM Communication",
    "Security", "Reserved",
};

static void write_reg(const char *name, uint64_t value) {
    console_write(name);
    console_write("=");
    console_write_hex(value);
    console_write(" ");
}

static void exception(uint64_t vector, struct interrupt_frame *frame, uint64_t error) {
    console_write("\nException ");
    console_write_u64(vector);
    console_write(": ");
    console_write(exception_names[vector]);
    console_write("\n");
    write_reg("RIP", frame->rip);
    write_reg("RSP", frame->rsp);
    write_reg("ERR", error);
    if (vector == 14) {
        uint64_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        write_reg("CR2", cr2); /* address of the page fault */
    }
    console_write("\n");
    panic("unbehandelte CPU-Exception");
}

/* The breakpoint (int3) is harmless and returns, handy for testing. */
__attribute__((interrupt))
static void isr_breakpoint(struct interrupt_frame *frame) {
    console_write("int3 bei ");
    console_write_hex(frame->rip);
    console_write(" abgefangen, IDT funktioniert\n");
}

/* One handler per vector, since the CPU does not pass the vector number. */
#define ISR_NOERR(n) \
    __attribute__((interrupt)) static void isr_##n(struct interrupt_frame *f) { exception(n, f, 0); }
#define ISR_ERR(n) \
    __attribute__((interrupt)) static void isr_##n(struct interrupt_frame *f, uint64_t e) { exception(n, f, e); }

ISR_NOERR(0)  ISR_NOERR(1)  ISR_NOERR(2)  ISR_NOERR(4)  ISR_NOERR(5)
ISR_NOERR(6)  ISR_NOERR(7)  ISR_ERR(8)    ISR_NOERR(9)  ISR_ERR(10)
ISR_ERR(11)   ISR_ERR(12)   ISR_ERR(13)   ISR_ERR(14)   ISR_NOERR(15)
ISR_NOERR(16) ISR_ERR(17)   ISR_NOERR(18) ISR_NOERR(19) ISR_NOERR(20)
ISR_ERR(21)   ISR_NOERR(22) ISR_NOERR(23) ISR_NOERR(24) ISR_NOERR(25)
ISR_NOERR(26) ISR_NOERR(27) ISR_NOERR(28) ISR_ERR(29)   ISR_ERR(30)
ISR_NOERR(31)

static void idt_set_gate(uint8_t vector, void *handler, uint8_t ist) {
    uint64_t addr = (uint64_t)handler;
    idt[vector] = (struct idt_entry){
        .offset_low = addr & 0xFFFF,
        .selector = GDT_KERNEL_CODE,
        .ist = ist,
        .type_attr = 0x8E, /* present, ring 0, interrupt gate */
        .offset_mid = (addr >> 16) & 0xFFFF,
        .offset_high = addr >> 32,
        .zero = 0,
    };
}

void arch_init(void) {
    gdt_init();

    void *handlers[32] = {
        isr_0, isr_1, isr_2, isr_breakpoint, isr_4, isr_5, isr_6, isr_7,
        isr_8, isr_9, isr_10, isr_11, isr_12, isr_13, isr_14, isr_15,
        isr_16, isr_17, isr_18, isr_19, isr_20, isr_21, isr_22, isr_23,
        isr_24, isr_25, isr_26, isr_27, isr_28, isr_29, isr_30, isr_31,
    };
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, handlers[i], i == 8 ? 1 : 0); /* double fault -> IST1 */
    }

    struct descriptor_ptr idtr = { sizeof(idt) - 1, (uint64_t)idt };
    __asm__ volatile ("lidt %0" : : "m"(idtr));

    /*
     * Move the legacy 8259 PIC to vectors 32-47 and mask it completely, so
     * hardware interrupts do not arrive looking like CPU exceptions.
     */
    outb(0x20, 0x11); outb(0xA0, 0x11); /* start initialisation */
    outb(0x21, 32);   outb(0xA1, 40);   /* vector offsets */
    outb(0x21, 4);    outb(0xA1, 2);    /* master/slave wiring */
    outb(0x21, 0x01); outb(0xA1, 0x01); /* 8086 mode */
    outb(0x21, 0xFF); outb(0xA1, 0xFF); /* mask everything */
}

void arch_breakpoint(void) {
    __asm__ volatile ("int3");
}

void arch_halt(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
