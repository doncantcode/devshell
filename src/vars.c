#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "shell.h"
#include "vars.h"

extern char **environ;

int g_last_status = 0;

typedef struct {
    char *name;
    char *value;
    int   exported;
} Var;

static Var *vars;
static int nvars, cap;

static Var *find(const char *name)
{
    for (int i = 0; i < nvars; i++)
        if (strcmp(vars[i].name, name) == 0)
            return &vars[i];
    return NULL;
}

static Var *add(const char *name, const char *value)
{
    if (nvars == cap) {
        int ncap = cap ? cap * 2 : 64;
        Var *p = realloc(vars, (size_t)ncap * sizeof *p);
        if (!p) {
            err_msg("out of memory");
            exit(1);
        }
        vars = p;
        cap = ncap;
    }
    vars[nvars].name = xstrdup(name);
    vars[nvars].value = xstrdup(value);
    vars[nvars].exported = 0;
    return &vars[nvars++];
}

int var_valid_name(const char *s, size_t len)
{
    if (len == 0 || !(isalpha((unsigned char)s[0]) || s[0] == '_'))
        return 0;
    for (size_t i = 1; i < len; i++)
        if (!(isalnum((unsigned char)s[i]) || s[i] == '_'))
            return 0;
    return 1;
}

void vars_init(void)
{
    for (char **e = environ; e && *e; e++) {
        const char *eq = strchr(*e, '=');
        if (!eq || eq == *e)
            continue;
        char *name = xstrdup(*e);
        name[eq - *e] = '\0';
        if (var_valid_name(name, strlen(name)) && !find(name))
            add(name, eq + 1)->exported = 1;
        free(name);
    }
}

void vars_free(void)
{
    for (int i = 0; i < nvars; i++) {
        free(vars[i].name);
        free(vars[i].value);
    }
    free(vars);
    vars = NULL;
    nvars = cap = 0;
}

const char *var_get(const char *name)
{
    Var *v = find(name);
    return v ? v->value : NULL;
}

int var_set(const char *name, const char *value)
{
    Var *v = find(name);

    if (!var_valid_name(name, strlen(name)))
        return -1;
    if (!v) {
        v = add(name, value);
    } else {
        char *copy = xstrdup(value);
        free(v->value);
        v->value = copy;
    }
    if (v->exported)
        setenv(name, value, 1);
    return 0;
}

int var_export(const char *name, const char *value)
{
    Var *v;

    if (!var_valid_name(name, strlen(name)))
        return -1;
    if (value)
        var_set(name, value);
    v = find(name);
    if (!v)
        v = add(name, "");
    v->exported = 1;
    setenv(name, v->value, 1);
    return 0;
}

int var_unset(const char *name)
{
    for (int i = 0; i < nvars; i++) {
        if (strcmp(vars[i].name, name) != 0)
            continue;
        if (vars[i].exported)
            unsetenv(name);
        free(vars[i].name);
        free(vars[i].value);
        vars[i] = vars[--nvars];
        return 0;
    }
    return 0;
}

int var_is_exported(const char *name)
{
    Var *v = find(name);
    return v && v->exported;
}

static int cmp_vars(const void *a, const void *b)
{
    return strcmp(((const Var *)a)->name, ((const Var *)b)->name);
}

void var_list(FILE *out, int exported_only)
{
    Var *copy;

    if (nvars == 0)
        return;
    copy = malloc((size_t)nvars * sizeof *copy);
    if (!copy)
        return;
    memcpy(copy, vars, (size_t)nvars * sizeof *copy);
    qsort(copy, (size_t)nvars, sizeof *copy, cmp_vars);

    for (int i = 0; i < nvars; i++) {
        if (exported_only) {
            if (copy[i].exported)
                fprintf(out, "export %s=\"%s\"\n", copy[i].name, copy[i].value);
        } else {
            fprintf(out, "%s=%s\n", copy[i].name, copy[i].value);
        }
    }
    free(copy);
}
