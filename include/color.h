#ifndef COLOR_H
#define COLOR_H

/* ANSI colours. Only ever emit them when color_ok(fd) says so, so pipes,
 * redirected files and the test script keep receiving plain text. */
#define C_RESET   "\033[0m"
#define C_BOLD    "\033[1m"
#define C_DIM     "\033[2m"
#define C_RED     "\033[31m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_BLUE    "\033[34m"
#define C_MAGENTA "\033[35m"
#define C_CYAN    "\033[36m"
#define C_BRED    "\033[1;31m"
#define C_BGREEN  "\033[1;32m"
#define C_BBLUE   "\033[1;34m"
#define C_BCYAN   "\033[1;36m"

/* Should output on file descriptor `fd` be coloured?
 * Yes when it is a terminal, TERM is not "dumb" and NO_COLOR is unset.
 * DEVSHELL_COLOR=always / never overrides that (used by the tests). */
int color_ok(int fd);

#endif
