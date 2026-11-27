#ifndef SHELL_H
#define SHELL_H

/*
 * Simple command line. Reads characters from the serial port and shows
 * everything both on the screen (console) and on the serial port.
 * Never returns.
 */
__attribute__((noreturn)) void shell_run(void);

#endif
