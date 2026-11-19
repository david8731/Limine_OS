#include "pmm.h"

#include <stddef.h>
#include <limine.h>

#include "memory/memory.h"
#include "boot/limine_requests.h"
#include "panic.h"

uint64_t hhdm_offset;

static uint8_t *bitmap;
static uint64_t bitmap_size;  /* in bytes */
static uint64_t total_pages;  /* pages up to the highest RAM address */
static uint64_t usable_pages; /* of those, pages that are really free RAM */
static uint64_t free_pages;
static uint64_t next_hint;    /* search starts here instead of at page 0 */

static void bit_set(uint64_t i)   { bitmap[i / 8] |= (uint8_t)(1 << (i % 8)); }
static void bit_clear(uint64_t i) { bitmap[i / 8] &= (uint8_t)~(1 << (i % 8)); }
static bool bit_test(uint64_t i)  { return bitmap[i / 8] & (1 << (i % 8)); }

bool pmm_init(void) {
    if (limine_memmap_request.response == NULL || limine_hhdm_request.response == NULL) {
        return false;
    }

    struct limine_memmap_response *map = limine_memmap_request.response;
    hhdm_offset = limine_hhdm_request.response->offset;

    /*
     * 1. Find the highest address of real RAM. Reserved ranges (device
     *    memory and the like) do not count, or the bitmap would get huge.
     */
    uint64_t highest = 0;
    for (uint64_t i = 0; i < map->entry_count; i++) {
        struct limine_memmap_entry *e = map->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE
         || e->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE
         || e->type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES) {
            if (e->base + e->length > highest) {
                highest = e->base + e->length;
            }
        }
    }
    total_pages = highest / PAGE_SIZE;
    bitmap_size = (total_pages + 7) / 8;

    /* 2. Find a usable range that can hold the bitmap itself, skipping page 0. */
    uint64_t bitmap_phys = 0;
    for (uint64_t i = 0; i < map->entry_count; i++) {
        struct limine_memmap_entry *e = map->entries[i];
        uint64_t base = e->base != 0 ? e->base : PAGE_SIZE;
        if (e->type == LIMINE_MEMMAP_USABLE && e->base + e->length >= base + bitmap_size) {
            bitmap_phys = base;
            break;
        }
    }
    if (bitmap_phys == 0) {
        return false;
    }
    bitmap = phys_to_virt(bitmap_phys);

    /*
     * 3. Mark everything as used, then free only the usable ranges. A bug here
     *    can at worst waste memory, never hand out memory something else uses.
     */
    memset(bitmap, 0xFF, bitmap_size);
    for (uint64_t i = 0; i < map->entry_count; i++) {
        struct limine_memmap_entry *e = map->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        for (uint64_t p = e->base / PAGE_SIZE; p < (e->base + e->length) / PAGE_SIZE; p++) {
            bit_clear(p);
            usable_pages++;
            free_pages++;
        }
    }

    /* 4. Mark the bitmap's own pages and page 0 as used again (0 = error value). */
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
    if (count == 0) {
        return 0;
    }

    /* First pass from the hint to the end, second pass from the start to the hint. */
    for (int pass = 0; pass < 2; pass++) {
        uint64_t start = pass == 0 ? next_hint : 0;
        uint64_t end = pass == 0 ? total_pages : next_hint;
        uint64_t run = 0;
        for (uint64_t i = start; i < end; i++) {
            run = bit_test(i) ? 0 : run + 1;
            if (run == count) {
                uint64_t first = i - count + 1;
                for (uint64_t j = first; j <= i; j++) {
                    bit_set(j);
                }
                free_pages -= count;
                next_hint = i + 1;
                return first * PAGE_SIZE;
            }
        }
    }
    return 0;
}

uint64_t pmm_alloc(void) {
    return pmm_alloc_pages(1);
}

void pmm_free_pages(uint64_t phys, uint64_t count) {
    uint64_t first = phys / PAGE_SIZE;
    for (uint64_t i = first; i < first + count; i++) {
        if (i >= total_pages || !bit_test(i)) {
            panic("PMM: double free or invalid address");
        }
        bit_clear(i);
        free_pages++;
    }
    if (first < next_hint) {
        next_hint = first;
    }
}

void pmm_free(uint64_t phys) {
    pmm_free_pages(phys, 1);
}

uint64_t pmm_total_pages(void) {
    return usable_pages;
}

uint64_t pmm_free_pages_count(void) {
    return free_pages;
}

bool pmm_selftest(void) {
    uint64_t before = free_pages;
    uint64_t a = pmm_alloc();
    uint64_t b = pmm_alloc();
    uint64_t c = pmm_alloc_pages(4);
    if (a == 0 || b == 0 || c == 0 || a == b) {
        return false;
    }

    /* Write through the HHDM to check the page is real, writable memory. */
    volatile uint64_t *p = phys_to_virt(a);
    *p = 0xDEADBEEFCAFEBABE;
    if (*p != 0xDEADBEEFCAFEBABE) {
        return false;
    }

    pmm_free(a);
    pmm_free(b);
    pmm_free_pages(c, 4);
    return free_pages == before;
}
