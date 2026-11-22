#ifndef ARCH_AARCH64_MMIO_H
#define ARCH_AARCH64_MMIO_H

#include <stddef.h>
#include <stdint.h>

/*
 * Make the 4 KiB page of device registers at phys reachable and return its
 * virtual address, or NULL if that fails. Used for the UART and the GIC.
 */
volatile uint32_t *map_device_page(uint64_t phys);

#endif
