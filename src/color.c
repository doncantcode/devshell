#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "color.h"

static int compute(int fd)
{
    const char *force = getenv("DEVSHELL_COLOR");
    const char *no_color = getenv("NO_COLOR");
    const char *term = getenv("TERM");

    if (force && strcmp(force, "always") == 0)
        return 1;
    if (force && strcmp(force, "never") == 0)
        return 0;
    if (no_color && *no_color)
        return 0;
    if (term && strcmp(term, "dumb") == 0)
        return 0;
    return isatty(fd);
}

int color_ok(int fd)
{
    return compute(fd);     /* cheap; recomputed so `export NO_COLOR=1` takes effect */
}
