#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#include "color.h"
#include "errors.h"
#include "fileops.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Parse leading -abc style options. Letters must be in `allowed`; seen
 * letters are recorded in flag[]. Returns the index of the first operand,
 * or -1 on an unknown option. */
static int parse_flags(const char *cmd, char **argv, const char *allowed, char flag[128])
{
    int i = 1;

    for (; argv[i] && argv[i][0] == '-' && argv[i][1]; i++) {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        for (const char *p = argv[i] + 1; *p; p++) {
            if (!strchr(allowed, *p)) {
                err_msg("%s: invalid option -- '%c'", cmd, *p);
                return -1;
            }
            flag[(unsigned char)*p & 127] = 1;
        }
    }
    return i;
}

/* ---------------------------------------------------------------- mkdir */

static int make_parents(const char *path)
{
    char buf[PATH_MAX];
    size_t len = strlen(path);

    if (len >= sizeof buf) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(buf, path, len + 1);

    for (size_t i = 1; i < len; i++) {
        if (buf[i] != '/')
            continue;
        buf[i] = '\0';
        if (mkdir(buf, 0755) < 0 && errno != EEXIST)
            return -1;
        buf[i] = '/';
    }
    if (mkdir(buf, 0755) < 0) {
        struct stat st;
        if (errno == EEXIST && stat(buf, &st) == 0 && S_ISDIR(st.st_mode))
            return 0;
        return -1;
    }
    return 0;
}

int fileops_mkdir(char **argv)
{
    char flag[128] = { 0 };
    int i = parse_flags("mkdir", argv, "p", flag), rc = 0;

    if (i < 0)
        return 1;
    if (!argv[i]) {
        err_msg("mkdir: missing operand");
        return 1;
    }
    for (; argv[i]; i++) {
        int r = flag['p'] ? make_parents(argv[i]) : mkdir(argv[i], 0755);
        if (r < 0) {
            err_sys("mkdir: cannot create directory '%s'", argv[i]);
            rc = 1;
        }
    }
    return rc;
}

/* ------------------------------------------------------------------- ls */

static int cmp_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void mode_string(mode_t m, char out[11])
{
    out[0] = S_ISDIR(m) ? 'd' : S_ISLNK(m) ? 'l' : '-';
    out[1] = (m & S_IRUSR) ? 'r' : '-';
    out[2] = (m & S_IWUSR) ? 'w' : '-';
    out[3] = (m & S_IXUSR) ? 'x' : '-';
    out[4] = (m & S_IRGRP) ? 'r' : '-';
    out[5] = (m & S_IWGRP) ? 'w' : '-';
    out[6] = (m & S_IXGRP) ? 'x' : '-';
    out[7] = (m & S_IROTH) ? 'r' : '-';
    out[8] = (m & S_IWOTH) ? 'w' : '-';
    out[9] = (m & S_IXOTH) ? 'x' : '-';
    out[10] = '\0';
}

/* Options for the current ls call (set in fileops_ls). */
static int ls_colour;   /* colour names by file type */
static int ls_human;    /* -h: 1.5K instead of 1536 */

static const char *type_color(const struct stat *st)
{
    if (S_ISDIR(st->st_mode))
        return C_BBLUE;
    if (S_ISLNK(st->st_mode))
        return C_CYAN;
    if (S_ISREG(st->st_mode) && (st->st_mode & 0111))
        return C_BGREEN;
    return NULL;
}

/* Print `name`, coloured by the type of the file at `path`. */
static void put_name(const char *path, const char *name)
{
    struct stat st;
    const char *c = NULL;

    if (ls_colour && lstat(path, &st) == 0)
        c = type_color(&st);
    if (c)
        printf("%s%s" C_RESET, c, name);
    else
        fputs(name, stdout);
}

static void size_string(long long n, char *out, size_t size)
{
    static const char units[] = "BKMGT";
    double v = (double)n;
    int u = 0;

    if (!ls_human) {
        snprintf(out, size, "%lld", n);
        return;
    }
    while (v >= 1024 && u < 4) {
        v /= 1024;
        u++;
    }
    if (u == 0)
        snprintf(out, size, "%lld", n);
    else
        snprintf(out, size, "%.1f%c", v, units[u]);
}

/* Print one entry in long format. `path` is what we stat, `name` what we show. */
static int print_long(const char *path, const char *name)
{
    struct stat st;
    char mode[11], when[32] = "", size[32];
    struct tm *tm;

    if (lstat(path, &st) < 0) {
        err_sys("ls: cannot access '%s'", path);
        return 1;
    }
    mode_string(st.st_mode, mode);
    size_string((long long)st.st_size, size, sizeof size);
    tm = localtime(&st.st_mtime);
    if (tm)
        strftime(when, sizeof when, "%b %d %H:%M", tm);
    printf("%s %8s %s ", mode, size, when);
    put_name(path, name);
    putchar('\n');
    return 0;
}

/* `dir` is the directory the names live in. Columns on a terminal,
 * one name per line otherwise (so `ls | wc -l` counts files). */
static void print_names(const char *dir, char **names, int n, int tty)
{
    int col = 0;

    for (int i = 0; i < n; i++) {
        char full[PATH_MAX];
        int w = (int)strlen(names[i]);

        snprintf(full, sizeof full, "%s/%s", dir, names[i]);
        if (!tty) {
            put_name(full, names[i]);
            putchar('\n');
            continue;
        }
        if (col && col + w + 2 > 80) {
            putchar('\n');
            col = 0;
        }
        if (col) {
            fputs("  ", stdout);
            col += 2;
        }
        put_name(full, names[i]);
        col += w;
    }
    if (tty && n)
        putchar('\n');
}

static int list_dir(const char *path, int all, int lng)
{
    DIR *d = opendir(path);
    struct dirent *e;
    char **names = NULL;
    int n = 0, cap = 0, rc = 0;

    if (!d) {
        err_sys("ls: cannot open directory '%s'", path);
        return 1;
    }
    while ((e = readdir(d))) {
        if (!all && e->d_name[0] == '.')
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            names = realloc(names, (size_t)cap * sizeof *names);
            if (!names) {
                err_msg("ls: out of memory");
                closedir(d);
                return 1;
            }
        }
        names[n++] = xstrdup(e->d_name);
    }
    closedir(d);
    if (n)
        qsort(names, (size_t)n, sizeof *names, cmp_names);

    if (lng) {
        for (int i = 0; i < n; i++) {
            char full[PATH_MAX];
            snprintf(full, sizeof full, "%s/%s", path, names[i]);
            rc |= print_long(full, names[i]);
        }
    } else {
        print_names(path, names, n, isatty(STDOUT_FILENO));
    }
    for (int i = 0; i < n; i++)
        free(names[i]);
    free(names);
    return rc;
}

int fileops_ls(char **argv)
{
    char flag[128] = { 0 };
    int i = parse_flags(argv[0], argv, "alh", flag), rc = 0;
    int nops, show_header;

    if (i < 0)
        return 1;
    ls_colour = color_ok(STDOUT_FILENO);
    ls_human = flag['h'];
    if (!argv[i]) {
        static char *dot[] = { ".", NULL };
        return list_dir(dot[0], flag['a'], flag['l']);
    }

    nops = 0;
    for (int j = i; argv[j]; j++)
        nops++;
    show_header = nops > 1;

    for (int first = 1; argv[i]; i++, first = 0) {
        struct stat st;
        if (stat(argv[i], &st) < 0) {
            err_sys("ls: cannot access '%s'", argv[i]);
            rc = 1;
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (show_header)
                printf("%s%s:\n", first ? "" : "\n", argv[i]);
            rc |= list_dir(argv[i], flag['a'], flag['l']);
        } else if (flag['l']) {
            rc |= print_long(argv[i], argv[i]);
        } else {
            put_name(argv[i], argv[i]);
            putchar('\n');
        }
    }
    return rc;
}

/* ---------------------------------------------------------------- touch */

int fileops_touch(char **argv)
{
    char flag[128] = { 0 };
    int i = parse_flags("touch", argv, "", flag), rc = 0;

    if (i < 0)
        return 1;
    if (!argv[i]) {
        err_msg("touch: missing file operand");
        return 1;
    }
    for (; argv[i]; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT | O_NOCTTY, 0644);
        if (fd < 0) {
            err_sys("touch: cannot touch '%s'", argv[i]);
            rc = 1;
            continue;
        }
        close(fd);
        if (utime(argv[i], NULL) < 0) {
            err_sys("touch: cannot touch '%s'", argv[i]);
            rc = 1;
        }
    }
    return rc;
}

/* ------------------------------------------------------------------ cat */

static int cat_stream(FILE *in, const char *name)
{
    char buf[4096];
    size_t n;

    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, stdout) != n) {
            err_sys("cat: write error");
            return 1;
        }
    }
    if (ferror(in)) {
        err_sys("cat: %s", name);
        return 1;
    }
    return 0;
}

/* Read from file descriptor 0 as it is *now*. The process-wide `stdin` FILE
 * must not be used: it may hold buffered input meant for the shell's REPL,
 * and in a forked child it would be read instead of the redirected fd 0. */
static int cat_fd0(void)
{
    int fd = dup(STDIN_FILENO), rc;
    FILE *f = fd >= 0 ? fdopen(fd, "r") : NULL;

    if (!f) {
        err_sys("cat: stdin");
        if (fd >= 0)
            close(fd);
        return 1;
    }
    rc = cat_stream(f, "stdin");
    fclose(f);
    return rc;
}

int fileops_cat(char **argv)
{
    int rc = 0;

    if (!argv[1])
        return cat_fd0();

    for (int i = 1; argv[i]; i++) {
        if (strcmp(argv[i], "-") == 0) {
            rc |= cat_fd0();
            continue;
        }
        FILE *f = fopen(argv[i], "r");
        if (!f) {
            err_sys("cat: %s", argv[i]);
            rc = 1;
            continue;
        }
        rc |= cat_stream(f, argv[i]);
        fclose(f);
    }
    return rc;
}

/* ------------------------------------------------------------------- rm */

static int rm_tree(const char *path, int recursive, int force)
{
    struct stat st;
    int rc = 0;

    if (lstat(path, &st) < 0) {
        if (force && errno == ENOENT)
            return 0;
        err_sys("rm: cannot remove '%s'", path);
        return 1;
    }

    if (!S_ISDIR(st.st_mode)) {
        if (unlink(path) < 0) {
            err_sys("rm: cannot remove '%s'", path);
            return 1;
        }
        return 0;
    }

    if (!recursive) {
        err_msg("rm: cannot remove '%s': Is a directory (use -r)", path);
        return 1;
    }

    DIR *d = opendir(path);
    struct dirent *e;
    if (!d) {
        err_sys("rm: cannot open directory '%s'", path);
        return 1;
    }
    while ((e = readdir(d))) {
        char child[PATH_MAX];
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        if (snprintf(child, sizeof child, "%s/%s", path, e->d_name) >= (int)sizeof child) {
            err_msg("rm: path too long under '%s'", path);
            rc = 1;
            continue;
        }
        rc |= rm_tree(child, recursive, force);
    }
    closedir(d);
    if (rmdir(path) < 0) {
        err_sys("rm: cannot remove '%s'", path);
        rc = 1;
    }
    return rc;
}

/* "/", ".", ".." and anything ending in /. or /.. */
static int is_protected(const char *path)
{
    size_t len = strlen(path);
    const char *base;

    while (len > 1 && path[len - 1] == '/')
        len--;
    if (len == 1 && path[0] == '/')
        return 1;
    base = path + len;
    while (base > path && base[-1] != '/')
        base--;
    len -= (size_t)(base - path);
    return (len == 1 && base[0] == '.') || (len == 2 && base[0] == '.' && base[1] == '.');
}

int fileops_rm(char **argv)
{
    char flag[128] = { 0 };
    int i = parse_flags("rm", argv, "rRf", flag), rc = 0;
    int recursive;

    if (i < 0)
        return 1;
    recursive = flag['r'] || flag['R'];
    if (!argv[i]) {
        if (flag['f'])
            return 0;
        err_msg("rm: missing operand");
        return 1;
    }
    for (; argv[i]; i++) {
        if (is_protected(argv[i])) {
            err_msg("rm: refusing to remove '%s'", argv[i]);
            rc = 1;
            continue;
        }
        rc |= rm_tree(argv[i], recursive, flag['f']);
    }
    return rc;
}
