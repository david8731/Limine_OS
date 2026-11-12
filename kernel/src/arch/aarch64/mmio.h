#ifndef AARCH64_MMIO_H
#define AARCH64_MMIO_H

#include <stdint.h>

/*
 * Map one 4 KiB page of device registers (e.g. the UART) at phys + hhdm_offset
 * and return its virtual address. Limine's HHDM only covers RAM, not devices.
 * Needs the PMM for new page tables. Returns NULL on failure.
 */
volatile void *map_device_page(uint64_t phys);

#endif
