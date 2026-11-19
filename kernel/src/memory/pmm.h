#ifndef PMM_H
#define PMM_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Physical memory manager: hands out 4 KiB pages of RAM.
 * One bit per page in a bitmap, 1 = used, 0 = free.
 */

#define PAGE_SIZE ((uint64_t)4096)

extern uint64_t hhdm_offset;

/* Physical address p is reachable through the pointer p + hhdm_offset. */
static inline void *phys_to_virt(uint64_t phys) {
    return (void *)(phys + hhdm_offset);
}

bool pmm_init(void);

/* PHYSICAL address of count free pages in a row, 0 if there is no memory left. */
uint64_t pmm_alloc_pages(uint64_t count);
void pmm_free_pages(uint64_t phys, uint64_t count);

static inline uint64_t pmm_alloc(void) { return pmm_alloc_pages(1); }
static inline void pmm_free(uint64_t phys) { pmm_free_pages(phys, 1); }

uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages_count(void);

#endif
