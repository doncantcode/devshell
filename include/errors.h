#ifndef ERRORS_H
#define ERRORS_H

#if defined(__GNUC__)
#define PRINTF_FMT(a, b) __attribute__((format(printf, a, b)))
#else
#define PRINTF_FMT(a, b)
#endif

/* "devshell: <message>" */
void err_msg(const char *fmt, ...) PRINTF_FMT(1, 2);

/* "devshell: <message>: <strerror(errno)>" */
void err_sys(const char *fmt, ...) PRINTF_FMT(1, 2);

/* "devshell: syntax error: <message>" */
void err_syntax(const char *fmt, ...) PRINTF_FMT(1, 2);

/* strdup that aborts on out-of-memory. */
char *xstrdup(const char *s);

#endif
