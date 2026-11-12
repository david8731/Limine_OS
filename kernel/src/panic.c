#include "panic.h"

#include "arch/arch.h"
#include "console/console.h"

void panic(const char *message) {
    console_write("\n*** KERNEL PANIC: ");
    console_write(message);
    console_write(" ***\n");
    arch_halt();
}
