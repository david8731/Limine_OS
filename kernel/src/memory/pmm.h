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

/* Build the bitmap from the Limine memory map. Returns false if that fails. */
bool pmm_init(void);

/* PHYSICAL address of n contiguous free pages, 0 if there is no memory left. */
uint64_t pmm_alloc_pages(uint64_t count);
uint64_t pmm_alloc(void);
void pmm_free_pages(uint64_t phys, uint64_t count);
void pmm_free(uint64_t phys);

uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages_count(void);

/* Allocate, write, free and check the counters. Returns true if all is well. */
bool pmm_selftest(void);

#endif
