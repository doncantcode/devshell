#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "color.h"
#include "trace.h"

int g_trace = 0;

/* One colour per phase, so a trace reads as a sequence of stages. */
static const char *tag_color(const char *tag)
{
    if (strcmp(tag, "lex") == 0)    return C_MAGENTA;
    if (strcmp(tag, "parse") == 0)  return C_BLUE;
    if (strcmp(tag, "expand") == 0) return C_YELLOW;
    if (strcmp(tag, "var") == 0)    return C_CYAN;
    if (strcmp(tag, "exec") == 0)   return C_GREEN;
    return C_DIM;                   /* wait, reap */
}

void trace(const char *tag, const char *fmt, ...)
{
    char label[16];
    va_list ap;

    if (!g_trace)
        return;
    fflush(stdout);
    snprintf(label, sizeof label, "[%s]", tag);
    if (color_ok(2)) {
        int pad = 9 - (int)strlen(label);
        fprintf(stderr, "%s%s%s%*s", tag_color(tag), label, C_RESET, pad > 0 ? pad : 0, "");
    } else
        fprintf(stderr, "%-9s", label);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void trace_tokens(const Token *toks, int n)
{
    char line[1024];
    size_t len = 0;

    if (!g_trace)
        return;
    line[0] = '\0';
    for (int i = 0; i < n && len < sizeof line - 1; i++) {
        int w;
        if (toks[i].type == TOK_WORD)
            w = snprintf(line + len, sizeof line - len, "%sWORD(%s)", i ? " " : "", toks[i].text);
        else
            w = snprintf(line + len, sizeof line - len, "%s%s", i ? " " : "", token_name(toks[i].type));
        if (w < 0)
            break;
        len += (size_t)w;
    }
    trace("lex", "%s", n ? line : "(no tokens)");
}

static const char *conn_text(Connector c)
{
    return c == CONN_AND ? "&&" : c == CONN_OR ? "||" : ";";
}

void trace_list(const CommandList *l)
{
    if (!g_trace)
        return;
    trace("parse", "%d pipeline%s", l->count, l->count == 1 ? "" : "s");
    for (int i = 0; i < l->count; i++) {
        const Pipeline *p = &l->items[i].pipe;

        trace("parse", "  #%d: %d stage%s%s", i + 1, p->count, p->count == 1 ? "" : "s",
              p->cmds[p->count - 1].background ? ", background" : "");
        for (int s = 0; s < p->count; s++) {
            const Command *c = &p->cmds[s];
            char argv[512];
            size_t len = 0;

            argv[0] = '\0';
            for (int a = 0; c->argv[a] && len < sizeof argv - 1; a++) {
                int w = snprintf(argv + len, sizeof argv - len, "%s%s", a ? ", " : "", c->argv[a]);
                if (w < 0)
                    break;
                len += (size_t)w;
            }
            trace("parse", "      stage %d: argv=[%s]%s%s%s%s", s, argv,
                  c->infile ? " in=" : "", c->infile ? c->infile : "",
                  c->outfile ? (c->append ? " out(append)=" : " out(truncate)=") : "",
                  c->outfile ? c->outfile : "");
        }
        if (i + 1 < l->count)
            trace("parse", "  then '%s' decides whether #%d runs", conn_text(l->items[i].next), i + 2);
    }
}
