#include "pmm.h"

#include <limine.h>

#include "memory/memory.h"
#include "boot/limine_requests.h"

uint64_t hhdm_offset;

static uint8_t *bitmap;
static uint64_t total_pages;  /* pages up to the highest RAM address */
static uint64_t usable_pages; /* of those, pages that are really free RAM */
static uint64_t free_pages;

static void bit_set(uint64_t i)   { bitmap[i / 8] |= (uint8_t)(1 << (i % 8)); }
static void bit_clear(uint64_t i) { bitmap[i / 8] &= (uint8_t)~(1 << (i % 8)); }
static bool bit_test(uint64_t i)  { return bitmap[i / 8] & (1 << (i % 8)); }

bool pmm_init(void) {
    if (limine_memmap_request.response == NULL || limine_hhdm_request.response == NULL) {
        return false;
    }
    struct limine_memmap_response *map = limine_memmap_request.response;
    hhdm_offset = limine_hhdm_request.response->offset;

    /* 1. Size of the bitmap: one bit per page up to the highest usable address. */
    uint64_t highest = 0;
    for (uint64_t i = 0; i < map->entry_count; i++) {
        struct limine_memmap_entry *e = map->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE && e->base + e->length > highest) {
            highest = e->base + e->length;
        }
    }
    total_pages = highest / PAGE_SIZE;
    uint64_t bitmap_size = (total_pages + 7) / 8;

    /* 2. Put the bitmap into the first usable range big enough, never at address 0. */
    uint64_t bitmap_phys = 0;
    for (uint64_t i = 0; i < map->entry_count && bitmap_phys == 0; i++) {
        struct limine_memmap_entry *e = map->entries[i];
        uint64_t start = e->base != 0 ? e->base : PAGE_SIZE;
        if (e->type == LIMINE_MEMMAP_USABLE && e->base + e->length >= start + bitmap_size) {
            bitmap_phys = start;
        }
    }
    if (bitmap_phys == 0) {
        return false;
    }
    bitmap = phys_to_virt(bitmap_phys);

    /* 3. Mark everything used, then free only what Limine reports as usable. */
    memset(bitmap, 0xFF, bitmap_size);
    for (uint64_t i = 0; i < map->entry_count; i++) {
        struct limine_memmap_entry *e = map->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE) {
            for (uint64_t p = e->base / PAGE_SIZE; p < (e->base + e->length) / PAGE_SIZE; p++) {
                bit_clear(p);
                usable_pages++;
                free_pages++;
            }
        }
    }

    /* 4. The bitmap's own pages and page 0 (0 means "no memory") stay used. */
    for (uint64_t p = 0; p < (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE; p++) {
        bit_set(bitmap_phys / PAGE_SIZE + p);
        free_pages--;
    }
    if (!bit_test(0)) {
        bit_set(0);
        free_pages--;
    }
    return true;
}

uint64_t pmm_alloc_pages(uint64_t count) {
    uint64_t run = 0; /* free pages found in a row so far */
    for (uint64_t i = 0; i < total_pages && count > 0; i++) {
        run = bit_test(i) ? 0 : run + 1;
        if (run == count) {
            uint64_t first = i - count + 1;
            for (uint64_t p = first; p <= i; p++) {
                bit_set(p);
            }
            free_pages -= count;
            return first * PAGE_SIZE;
        }
    }
    return 0;
}

void pmm_free_pages(uint64_t phys, uint64_t count) {
    for (uint64_t p = phys / PAGE_SIZE; p < phys / PAGE_SIZE + count; p++) {
        if (p < total_pages && bit_test(p)) {
            bit_clear(p);
            free_pages++;
        }
    }
}

uint64_t pmm_total_pages(void) {
    return usable_pages;
}

uint64_t pmm_free_pages_count(void) {
    return free_pages;
}
