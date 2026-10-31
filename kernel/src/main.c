#include <stdint.h>
#include <stdbool.h>
#include <limine.h>

#include "boot/limine_requests.h"
#include "console/console.h"
#include "memory/memory_info.h"

static void halt_forever(void) {
    for (;;) {
#if defined(__x86_64__)
        __asm__ volatile ("hlt");
#elif defined(__aarch64__) || defined(__riscv)
        __asm__ volatile ("wfi");
#elif defined(__loongarch64)
        __asm__ volatile ("idle 0");
#else
        for (;;) { }
#endif
    }
}

void kmain(void) {
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        halt_forever();
    }

    if (!console_init()) {
        /* No framebuffer terminal is available, so there is nowhere to log. */
        halt_forever();
    }

    console_write("\033[2J\033[H");
    console_write("LimiTask terminal\n");
    console_write("==============================================\n");
    console_write("Kernel booted successfully.\n");
    console_write("\n");

    memory_info_print();

    halt_forever();
}
