#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "color.h"
#include "errors.h"

static void vreport(const char *prefix, int with_errno, const char *fmt, va_list ap)
{
    int saved = errno;
    int colour = color_ok(2);

    fflush(stdout);
    if (colour)
        fprintf(stderr, C_BRED "devshell:" C_RESET " ");
    else
        fprintf(stderr, "devshell: ");
    fprintf(stderr, "%s", prefix);
    vfprintf(stderr, fmt, ap);
    if (with_errno)
        fprintf(stderr, ": %s", strerror(saved));
    fputc('\n', stderr);
}

void err_msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vreport("", 0, fmt, ap);
    va_end(ap);
}

void err_sys(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vreport("", 1, fmt, ap);
    va_end(ap);
}

void err_syntax(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vreport("syntax error: ", 0, fmt, ap);
    va_end(ap);
}

char *xstrdup(const char *s)
{
    char *p = strdup(s);
    if (!p) {
        fputs("devshell: out of memory\n", stderr);
        exit(1);
    }
    return p;
}
