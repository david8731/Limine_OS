#include <stdint.h>
#include <stdbool.h>
#include <limine.h>

#include "boot/limine_requests.h"
#include "console/console.h"
#include "memory/memory_info.h"
#include "memory/pmm.h"
#include "arch/arch.h"
#include "shell/shell.h"

static void halt_forever(void) {
    for (;;) {
        __asm__ volatile ("wfi"); /* wait for interrupt: sleep until something happens */
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

    /* The PMM comes first: on aarch64 mapping the UART needs a page. */
    if (!pmm_init()) {
        console_write("Fehler: PMM konnte nicht eingerichtet werden\n");
        halt_forever();
    }
    if (!serial_init()) {
        console_write("Serielle Schnittstelle nicht gefunden, keine Eingabe moeglich\n");
    }

    shell_run();
}
