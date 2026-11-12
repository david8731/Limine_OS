#include "gdt.h"

#include <stdint.h>

#include "memory/memory.h"

/*
 * Limine brings its own GDT, but it lives in bootloader memory and has no
 * user segments. This one stays valid and is ready for user processes.
 */

struct tss {
    uint32_t reserved0;
    uint64_t rsp[3];     /* rsp[0]: kernel stack when switching ring 3 -> ring 0 */
    uint64_t reserved1;
    uint64_t ist[7];     /* emergency stacks for single interrupts */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct descriptor_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static uint64_t gdt[7]; /* 5 segments + TSS (takes 2 entries) */
static struct tss tss;
static uint8_t double_fault_stack[4096 * 4] __attribute__((aligned(16)));

/* access: present | DPL | type, flags: granularity | long mode */
static uint64_t gdt_entry(uint8_t access, uint8_t flags) {
    return ((uint64_t)access << 40) | ((uint64_t)flags << 52) | 0x000F00000000FFFF;
}

void gdt_init(void) {
    gdt[0] = 0;
    gdt[1] = gdt_entry(0x9A, 0xA); /* kernel code, ring 0, 64 bit */
    gdt[2] = gdt_entry(0x92, 0xC); /* kernel data, ring 0 */
    gdt[3] = gdt_entry(0xF2, 0xC); /* user data, ring 3 */
    gdt[4] = gdt_entry(0xFA, 0xA); /* user code, ring 3, 64 bit */

    memset(&tss, 0, sizeof(tss));
    tss.ist[0] = (uint64_t)double_fault_stack + sizeof(double_fault_stack);
    tss.iomap_base = sizeof(tss);

    uint64_t base = (uint64_t)&tss;
    uint64_t limit = sizeof(tss) - 1;
    gdt[5] = (limit & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (0x89ULL << 40)
           | (((limit >> 16) & 0xF) << 48) | (((base >> 24) & 0xFF) << 56);
    gdt[6] = base >> 32;

    struct descriptor_ptr gdtr = { sizeof(gdt) - 1, (uint64_t)gdt };
    __asm__ volatile (
        "lgdt %0\n"
        /* CS cannot be loaded with mov, so use a far return */
        "push %1\n"
        "lea 1f(%%rip), %%rax\n"
        "push %%rax\n"
        "lretq\n"
        "1:\n"
        "mov %2, %%ds\n"
        "mov %2, %%es\n"
        "mov %2, %%fs\n"
        "mov %2, %%gs\n"
        "mov %2, %%ss\n"
        "ltr %w3\n"
        :
        : "m"(gdtr), "i"(GDT_KERNEL_CODE), "r"((uint64_t)GDT_KERNEL_DATA), "r"((uint64_t)GDT_TSS)
        : "rax", "memory");
}
