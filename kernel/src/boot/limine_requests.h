#ifndef MINIOS_LIMINE_REQUESTS_H
#define MINIOS_LIMINE_REQUESTS_H

#include <stdint.h>
#include <limine.h>

extern volatile uint64_t limine_base_revision[];
extern volatile struct limine_framebuffer_request limine_framebuffer_request;
extern volatile struct limine_memmap_request limine_memmap_request;
extern volatile struct limine_hhdm_request limine_hhdm_request;
extern volatile struct limine_flanterm_fb_init_params_request limine_flanterm_params_request;

#endif
