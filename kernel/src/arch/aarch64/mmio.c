#include "mmio.h"

#include <stddef.h>

#include "memory/memory.h"
#include "memory/pmm.h"

/*
 * Limine uses 4 KiB pages and 4 levels of page tables (L0 to L3) with 512
 * entries each. TTBR1_EL1 points to the L0 table of the upper address half.
 */
#define PTE_VALID     (1ULL << 0)
#define PTE_TABLE     (1ULL << 1)  /* L0-L2: points to next table, L3: page */
#define PTE_AF        (1ULL << 10) /* access flag, otherwise the first access faults */
#define PTE_PXN       (1ULL << 53) /* not executable by the kernel */
#define PTE_UXN       (1ULL << 54) /* not executable by user code */
#define PTE_ADDR_MASK 0x0000FFFFFFFFF000ULL

/*
 * MAIR_EL1 holds 8 memory types. Device registers need type 0x00
 * (Device-nGnRnE): no caching, no reordering of accesses.
 */
static int mair_device_index(void) {
    uint64_t mair;
    __asm__ volatile ("mrs %0, mair_el1" : "=r"(mair));
    for (int i = 0; i < 8; i++) {
        if (((mair >> (i * 8)) & 0xFF) == 0x00) {
            return i;
        }
    }
    return -1;
}

volatile void *map_device_page(uint64_t phys) {
    int attr = mair_device_index();
    if (attr < 0) {
        return NULL;
    }

    uint64_t virt = phys + hhdm_offset;
    uint64_t ttbr1;
    __asm__ volatile ("mrs %0, ttbr1_el1" : "=r"(ttbr1));
    uint64_t *table = phys_to_virt(ttbr1 & PTE_ADDR_MASK);

    /* Walk L0 to L2 and create missing tables with pages from the PMM. */
    for (int level = 0; level < 3; level++) {
        uint64_t index = (virt >> (39 - level * 9)) & 0x1FF;
        uint64_t entry = table[index];
        if ((entry & PTE_VALID) == 0) {
            uint64_t page = pmm_alloc();
            if (page == 0) {
                return NULL;
            }
            memset(phys_to_virt(page), 0, PAGE_SIZE);
            entry = page | PTE_TABLE | PTE_VALID;
            table[index] = entry;
        } else if ((entry & PTE_TABLE) == 0) {
            return NULL; /* already covered by a large block mapping */
        }
        table = phys_to_virt(entry & PTE_ADDR_MASK);
    }

    /* L3: the page itself, as device memory and not executable. */
    table[(virt >> 12) & 0x1FF] = (phys & PTE_ADDR_MASK) | PTE_VALID | PTE_TABLE
                                | ((uint64_t)attr << 2) | PTE_AF | PTE_PXN | PTE_UXN;

    /* Make sure the CPU sees the new entry before the first access. */
    __asm__ volatile ("dsb ishst\n tlbi vaae1is, %0\n dsb ish\n isb" : : "r"(virt >> 12) : "memory");
    return (volatile void *)virt;
}
