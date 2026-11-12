#ifndef PANIC_H
#define PANIC_H

/* Print the message and stop the kernel. For errors it cannot continue after. */
__attribute__((noreturn)) void panic(const char *message);

#endif
