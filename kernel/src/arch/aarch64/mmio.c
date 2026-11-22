#include "arch/aarch64/mmio.h"

#include "memory/memory.h"
#include "memory/pmm.h"

/* Page table entry bits (4 KiB pages, 4 levels L0 to L3). */
#define PTE_VALID     (1ULL << 0)
#define PTE_TABLE     (1ULL << 1)
#define PTE_AF        (1ULL << 10)
#define PTE_NO_EXEC   ((1ULL << 53) | (1ULL << 54))
#define PTE_ADDR_MASK 0x0000FFFFFFFFF000ULL

/*
 * Limine's HHDM only maps RAM, not device registers. So enter device pages
 * (UART, GIC) into the page tables ourselves, at phys + hhdm_offset like
 * everything else.
 */
volatile uint32_t *map_device_page(uint64_t phys) {
    /* Find the "device memory" type (0x00) among the 8 types in MAIR_EL1. */
    uint64_t mair;
    __asm__ volatile ("mrs %0, mair_el1" : "=r"(mair));
    int attr = 0;
    while (attr < 8 && ((mair >> (attr * 8)) & 0xFF) != 0) {
        attr++;
    }
    if (attr == 8) {
        return NULL;
    }

    uint64_t virt = phys + hhdm_offset;
    uint64_t ttbr1;
    __asm__ volatile ("mrs %0, ttbr1_el1" : "=r"(ttbr1));
    uint64_t *table = phys_to_virt(ttbr1 & PTE_ADDR_MASK);

    /* Walk L0 to L2, creating missing tables with pages from the PMM. */
    for (int level = 0; level < 3; level++) {
        uint64_t *entry = &table[(virt >> (39 - level * 9)) & 0x1FF];
        if ((*entry & PTE_VALID) == 0) {
            uint64_t page = pmm_alloc();
            if (page == 0) {
                return NULL;
            }
            memset(phys_to_virt(page), 0, PAGE_SIZE);
            *entry = page | PTE_TABLE | PTE_VALID;
        }
        table = phys_to_virt(*entry & PTE_ADDR_MASK);
    }

    /* L3: the device page itself, as device memory, never executable. */
    table[(virt >> 12) & 0x1FF] = phys | PTE_VALID | PTE_TABLE
                                | ((uint64_t)attr << 2) | PTE_AF | PTE_NO_EXEC;
    __asm__ volatile ("dsb ishst\n tlbi vaae1is, %0\n dsb ish\n isb" : : "r"(virt >> 12) : "memory");
    return (volatile uint32_t *)virt;
}
