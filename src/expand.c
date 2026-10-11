#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "errors.h"
#include "expand.h"
#include "trace.h"
#include "vars.h"

typedef struct {
    char  *p;
    size_t len, cap;
} Buf;

static void buf_add(Buf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 64;
        while (ncap < b->len + n + 1)
            ncap *= 2;
        b->p = realloc(b->p, ncap);
        if (!b->p) {
            err_msg("out of memory");
            exit(1);
        }
        b->cap = ncap;
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

static void buf_putc(Buf *b, char c)
{
    buf_add(b, &c, 1);
}

/* raw[*i] is '$'. Appends the expansion and advances *i; returns 1 if
 * something was expanded, 0 if the '$' is literal. */
static int expand_dollar(const char *raw, size_t *i, Buf *b)
{
    char tmp[32];
    size_t j = *i + 1;

    if (raw[j] == '?') {
        snprintf(tmp, sizeof tmp, "%d", g_last_status);
        buf_add(b, tmp, strlen(tmp));
        *i = j + 1;
        return 1;
    }
    if (raw[j] == '$') {
        snprintf(tmp, sizeof tmp, "%d", (int)getpid());
        buf_add(b, tmp, strlen(tmp));
        *i = j + 1;
        return 1;
    }
    if (raw[j] == '{') {
        const char *close = strchr(raw + j + 1, '}');
        size_t len;
        char *name;
        const char *val;

        if (!close || !var_valid_name(raw + j + 1, (size_t)(close - (raw + j + 1)))) {
            buf_putc(b, '$');
            *i = j;
            return 0;
        }
        len = (size_t)(close - (raw + j + 1));
        name = malloc(len + 1);
        if (!name)
            return 0;
        memcpy(name, raw + j + 1, len);
        name[len] = '\0';
        val = var_get(name);
        if (val)
            buf_add(b, val, strlen(val));
        free(name);
        *i = (size_t)(close - raw) + 1;
        return 1;
    }
    if (isalpha((unsigned char)raw[j]) || raw[j] == '_') {
        size_t len = 1;
        char *name;
        const char *val;

        while (isalnum((unsigned char)raw[j + len]) || raw[j + len] == '_')
            len++;
        name = malloc(len + 1);
        if (!name)
            return 0;
        memcpy(name, raw + j, len);
        name[len] = '\0';
        val = var_get(name);
        if (val)
            buf_add(b, val, strlen(val));
        free(name);
        *i = j + len;
        return 1;
    }
    buf_putc(b, '$');           /* lone '$': literal */
    *i = j;
    return 0;
}

char *expand_word(const char *raw, int *removed)
{
    Buf b = { NULL, 0, 0 };
    int quoted = 0, expanded = 0;
    size_t i = 0;

    buf_add(&b, "", 0);         /* make sure p is a valid empty string */
    while (raw[i]) {
        char c = raw[i];

        if (c == '\'') {
            quoted = 1;
            i++;
            while (raw[i] && raw[i] != '\'')
                buf_putc(&b, raw[i++]);
            if (raw[i])
                i++;
        } else if (c == '"') {
            quoted = 1;
            i++;
            while (raw[i] && raw[i] != '"') {
                if (raw[i] == '\\' &&
                    (raw[i + 1] == '"' || raw[i + 1] == '\\' || raw[i + 1] == '$')) {
                    buf_putc(&b, raw[i + 1]);
                    i += 2;
                } else if (raw[i] == '$') {
                    if (expand_dollar(raw, &i, &b))
                        expanded = 1;
                } else {
                    buf_putc(&b, raw[i++]);
                }
            }
            if (raw[i])
                i++;
        } else if (c == '\\') {
            quoted = 1;
            if (raw[i + 1]) {
                buf_putc(&b, raw[i + 1]);
                i += 2;
            } else {
                i++;
            }
        } else if (c == '$') {
            if (expand_dollar(raw, &i, &b))
                expanded = 1;
        } else {
            buf_putc(&b, c);
            i++;
        }
    }

    if (removed)
        *removed = (b.len == 0 && expanded && !quoted);
    return b.p;
}

static int expand_command(Command *c)
{
    int out = 0;

    for (int i = 0; c->argv[i]; i++) {
        int removed = 0;
        char *raw = c->argv[i];
        char *val = expand_word(raw, &removed);

        if (g_trace && (removed || strcmp(raw, val) != 0))
            trace("expand", "%s -> %s", raw, removed ? "(nothing: word removed)" : val);
        free(raw);
        if (removed) {
            free(val);
            c->argv[i] = NULL;
        } else {
            c->argv[i] = val;
        }
    }
    /* compact away removed words */
    for (int i = 0; i < MAX_ARGS; i++)
        if (c->argv[i])
            c->argv[out++] = c->argv[i];
    for (int i = out; i < MAX_ARGS; i++)
        c->argv[i] = NULL;

    char **targets[2] = { &c->infile, &c->outfile };
    for (int t = 0; t < 2; t++) {
        int removed = 0;
        char *raw = *targets[t], *val;

        if (!raw)
            continue;
        val = expand_word(raw, &removed);
        if (removed || *val == '\0') {
            err_msg("%s: ambiguous redirect", raw);
            free(val);
            return -1;
        }
        if (g_trace && strcmp(raw, val) != 0)
            trace("expand", "%s -> %s", raw, val);
        free(raw);
        *targets[t] = val;
    }
    return 0;
}

int expand_pipeline(Pipeline *p)
{
    for (int i = 0; i < p->count; i++)
        if (expand_command(&p->cmds[i]) < 0)
            return -1;
    return 0;
}

int is_assignment(const char *raw)
{
    const char *eq = strchr(raw, '=');
    return eq && var_valid_name(raw, (size_t)(eq - raw));
}
