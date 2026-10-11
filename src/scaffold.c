#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "errors.h"
#include "scaffold.h"
#include "vars.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define PLACEHOLDER "{{NAME}}"

static const struct {
    const char *type;   /* what the user types */
    const char *dir;    /* folder under templates/ */
    const char *desc;
} types[] = {
    { "c-project",      "c",      "C project with a Makefile" },
    { "python-project", "python", "Python project with main.py" },
};

#define NTYPES (sizeof types / sizeof types[0])

static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Look for templates/ via $DEVSHELL_TEMPLATES, next to the executable,
 * one level above it (when running build/devshell), then ./templates. */
static int find_templates(char *out, size_t size)
{
    const char *env = var_get("DEVSHELL_TEMPLATES");
    char exe[PATH_MAX];
    ssize_t n;

    if (env && is_dir(env)) {
        snprintf(out, size, "%s", env);
        return 0;
    }
    n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        char *slash;
        exe[n] = '\0';
        slash = strrchr(exe, '/');
        if (slash) {
            *slash = '\0';
            snprintf(out, size, "%s/templates", exe);
            if (is_dir(out))
                return 0;
            snprintf(out, size, "%s/../templates", exe);
            if (is_dir(out))
                return 0;
        }
    }
    snprintf(out, size, "templates");
    return is_dir(out) ? 0 : -1;
}

static int valid_name(const char *name)
{
    if (!*name || name[0] == '-' || name[0] == '.')
        return 0;
    for (const char *p = name; *p; p++) {
        char c = *p;
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok)
            return 0;
    }
    return 1;
}

/* Copy one file, replacing every {{NAME}} with `name`. */
static int copy_file(const char *src, const char *dst, mode_t mode, const char *name)
{
    FILE *in = fopen(src, "rb"), *out;
    char *data;
    long size;
    size_t got, plen = strlen(PLACEHOLDER), nlen = strlen(name);
    const char *p, *hit;
    int rc = 0;

    if (!in) {
        err_sys("new: cannot read template '%s'", src);
        return -1;
    }
    fseek(in, 0, SEEK_END);
    size = ftell(in);
    rewind(in);
    if (size < 0 || !(data = malloc((size_t)size + 1))) {
        fclose(in);
        err_msg("new: cannot read template '%s'", src);
        return -1;
    }
    got = fread(data, 1, (size_t)size, in);
    fclose(in);
    data[got] = '\0';

    out = fopen(dst, "wb");
    if (!out) {
        err_sys("new: cannot create '%s'", dst);
        free(data);
        return -1;
    }
    p = data;
    while ((hit = strstr(p, PLACEHOLDER))) {
        fwrite(p, 1, (size_t)(hit - p), out);
        fwrite(name, 1, nlen, out);
        p = hit + plen;
    }
    fputs(p, out);
    if (fclose(out) != 0) {
        err_sys("new: cannot write '%s'", dst);
        rc = -1;
    }
    chmod(dst, mode & 0777);
    free(data);
    return rc;
}

static int copy_tree(const char *src, const char *dst, const char *name)
{
    DIR *d = opendir(src);
    struct dirent *e;
    int rc = 0;

    if (!d) {
        err_sys("new: cannot read template '%s'", src);
        return -1;
    }
    while ((e = readdir(d))) {
        char s[PATH_MAX], t[PATH_MAX];
        struct stat st;

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        if (snprintf(s, sizeof s, "%s/%s", src, e->d_name) >= (int)sizeof s ||
            snprintf(t, sizeof t, "%s/%s", dst, e->d_name) >= (int)sizeof t) {
            err_msg("new: path too long");
            rc = -1;
            break;
        }
        if (stat(s, &st) < 0) {
            err_sys("new: %s", s);
            rc = -1;
            break;
        }
        if (S_ISDIR(st.st_mode)) {
            if (mkdir(t, 0755) < 0) {
                err_sys("new: cannot create '%s'", t);
                rc = -1;
                break;
            }
            if (copy_tree(s, t, name) < 0) {
                rc = -1;
                break;
            }
        } else if (copy_file(s, t, st.st_mode, name) < 0) {
            rc = -1;
            break;
        }
    }
    closedir(d);
    return rc;
}

static void usage(void)
{
    fputs("usage: new <type> <name>\ntypes:\n", stderr);
    for (size_t i = 0; i < NTYPES; i++)
        fprintf(stderr, "  %-15s %s\n", types[i].type, types[i].desc);
}

int scaffold_new(char **argv)
{
    char root[PATH_MAX], src[PATH_MAX];
    const char *dir = NULL, *name;

    if (!argv[1] || !argv[2] || argv[3]) {
        usage();
        return 1;
    }
    for (size_t i = 0; i < NTYPES; i++)
        if (strcmp(argv[1], types[i].type) == 0)
            dir = types[i].dir;
    if (!dir) {
        err_msg("new: unknown project type '%s'", argv[1]);
        usage();
        return 1;
    }
    name = argv[2];
    if (!valid_name(name)) {
        err_msg("new: invalid project name '%s' (use letters, digits, _ - .)", name);
        return 1;
    }
    if (find_templates(root, sizeof root) < 0) {
        err_msg("new: templates directory not found (set DEVSHELL_TEMPLATES)");
        return 1;
    }
    if (snprintf(src, sizeof src, "%s/%s", root, dir) >= (int)sizeof src || !is_dir(src)) {
        err_msg("new: missing template '%s'", src);
        return 1;
    }

    if (mkdir(name, 0755) < 0) {
        err_sys("new: cannot create '%s'", name);
        return 1;
    }
    if (copy_tree(src, name, name) < 0)
        return 1;

    printf("Created %s '%s'\n  cd %s\n", argv[1], name, name);
    return 0;
}
