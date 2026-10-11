#include <errno.h>
#include <limits.h>
#include <stdio.h>      /* before readline: its headers need FILE */
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_READLINE
#include <readline/history.h>
#include <readline/readline.h>
#endif

#include "color.h"
#include "errors.h"
#include "shell.h"
#include "ui.h"
#include "vars.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* ------------------------------------------------------------- banner */

/* Quick-start entries, printed two per row. */
static const struct {
    const char *cmd;
    const char *desc;
} quick[] = {
    { "ls",                   "list files" },
    { "tour",                 "guided walkthrough" },
    { "echo hi | tr a-z A-Z", "pipe two commands" },
    { "help [cmd]",           "commands, or details" },
    { "X=5; echo $X",         "variables" },
    { "trace on",             "watch fork/exec/wait" },
    { "new c-project demo",   "start a project" },
    { "exit",                 "leave" },
};

#define NQUICK (sizeof quick / sizeof quick[0])

static void put_cell(const char *text, int width, const char *colour, int use_colour)
{
    int pad = width - (int)strlen(text);

    if (use_colour && colour)
        printf("%s%s" C_RESET, colour, text);
    else
        fputs(text, stdout);
    printf("%*s", pad > 0 ? pad : 0, "");
}

void ui_banner(void)
{
    int c = color_ok(1);

    if (c)
        printf(C_BCYAN "DevShell %s" C_RESET " - a small developer shell\n", DEVSHELL_VERSION);
    else
        printf("DevShell %s - a small developer shell\n", DEVSHELL_VERSION);

    for (size_t i = 0; i < NQUICK; i += 2) {
        fputs("  ", stdout);
        put_cell(quick[i].cmd, 22, C_CYAN, c);
        put_cell(quick[i].desc, 22, NULL, c);
        if (i + 1 < NQUICK) {
            put_cell(quick[i + 1].cmd, 11, C_CYAN, c);
            put_cell(quick[i + 1].desc, 0, NULL, c);
        }
        putchar('\n');
    }
    if (c)
        printf(C_DIM "  Hide this with ./devshell -q (or DEVSHELL_QUIET=1); "
               "'welcome' shows it again." C_RESET "\n\n");
    else
        printf("  Hide this with ./devshell -q (or DEVSHELL_QUIET=1); "
               "'welcome' shows it again.\n\n");
}

/* ------------------------------------------------------------- prompt */

static int use_readline(void);

/* "<text>> " in `colour` for short secondary prompts such as the tour's. */
void ui_simple_prompt(char *buf, size_t size, const char *text, const char *colour)
{
    if (color_ok(1) && use_readline())
        snprintf(buf, size, "\001%s\002%s>\001" C_RESET "\002 ", colour, text);
    else
        snprintf(buf, size, "%s> ", text);
}

/* Read a small text file into buf. */
static int slurp(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "r");
    size_t n;

    if (!f)
        return -1;
    n = fread(buf, 1, size - 1, f);
    fclose(f);
    buf[n] = '\0';
    return 0;
}

/* Branch name of the repository containing the current directory, found by
 * reading .git/HEAD directly (no git executable needed). Returns 0 if found. */
static int git_branch(char *out, size_t size)
{
    char dir[PATH_MAX], path[PATH_MAX + 16], head[512];

    if (!getcwd(dir, sizeof dir))
        return -1;

    for (int depth = 0; depth < 64; depth++) {
        struct stat st;
        char *slash;

        snprintf(path, sizeof path, "%s/.git", dir);
        if (stat(path, &st) == 0) {
            if (S_ISREG(st.st_mode)) {        /* worktree/submodule: "gitdir: <path>" */
                char ptr[PATH_MAX];
                if (slurp(path, ptr, sizeof ptr) < 0 || strncmp(ptr, "gitdir:", 7) != 0)
                    return -1;
                char *p = ptr + 7;
                while (*p == ' ')
                    p++;
                p[strcspn(p, "\r\n")] = '\0';
                if (*p == '/')
                    snprintf(path, sizeof path, "%s/HEAD", p);
                else
                    snprintf(path, sizeof path, "%s/%s/HEAD", dir, p);
            } else {
                snprintf(path, sizeof path, "%s/.git/HEAD", dir);
            }
            if (slurp(path, head, sizeof head) < 0)
                return -1;
            head[strcspn(head, "\r\n")] = '\0';
            if (strncmp(head, "ref: ", 5) == 0) {
                const char *ref = head + 5;
                if (strncmp(ref, "refs/heads/", 11) == 0)
                    ref += 11;
                snprintf(out, size, "%s", ref);
            } else {
                snprintf(out, size, "%.7s", head);   /* detached HEAD: short hash */
            }
            return 0;
        }
        slash = strrchr(dir, '/');
        if (!slash || slash == dir)
            return -1;
        *slash = '\0';
    }
    return -1;
}

static const char *short_cwd(char *buf, size_t size)
{
    const char *home = var_get("HOME");

    if (!getcwd(buf, size))
        return "?";
    if (home && *home && strncmp(buf, home, strlen(home)) == 0 &&
        (buf[strlen(home)] == '/' || buf[strlen(home)] == '\0')) {
        size_t hl = strlen(home);
        memmove(buf + 1, buf + hl, strlen(buf + hl) + 1);
        buf[0] = '~';
    }
    return buf;
}

/* readline must be told which bytes take no screen width (\001 ... \002),
 * or it mis-measures the prompt and breaks line editing. Those markers are
 * stripped again when the prompt is not going to readline. */
#define COL(c) "\001" c "\002"

#ifdef HAVE_READLINE
static int use_readline(void)
{
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}
#else
static int use_readline(void)
{
    return 0;
}
#endif

void ui_build_prompt(char *buf, size_t size)
{
    char cwd[PATH_MAX], branch[256];
    int colour = color_ok(1);
    int have_branch = git_branch(branch, sizeof branch) == 0;
    const char *dir = short_cwd(cwd, sizeof cwd);
    size_t len = 0;

#define ADD(...) do { if (len < size) { \
        int w_ = snprintf(buf + len, size - len, __VA_ARGS__); \
        if (w_ > 0) len += (size_t)w_; } } while (0)

    if (colour) {
        ADD(COL(C_BCYAN) "devshell" COL(C_RESET) " " COL(C_BBLUE) "%s" COL(C_RESET), dir);
        if (have_branch)
            ADD(" " COL(C_YELLOW) "(%s)" COL(C_RESET), branch);
        if (g_last_status != 0)
            ADD(" " COL(C_BRED) "[%d]" COL(C_RESET), g_last_status);
        ADD(" %s>" COL(C_RESET) " ", g_last_status ? COL(C_RED) : COL(C_GREEN));
    } else {
        ADD("devshell %s", dir);
        if (have_branch)
            ADD(" (%s)", branch);
        if (g_last_status != 0)
            ADD(" [%d]", g_last_status);
        ADD("> ");
    }
#undef ADD

    if (!use_readline()) {
        char *w = buf;
        for (const char *r = buf; *r; r++)
            if (*r != '\001' && *r != '\002')
                *w++ = *r;
        *w = '\0';
    }
}

/* --------------------------------------------------------- reading */

#ifdef HAVE_READLINE

static char *history_path(char *buf, size_t size)
{
    const char *home = var_get("HOME");

    if (!home || !*home)
        return NULL;
    snprintf(buf, size, "%s/.devshell_history", home);
    return buf;
}

int ui_has_history(void)
{
    return 1;
}

void ui_history_add(const char *line)
{
    HIST_ENTRY *last;

    if (!use_readline() || !*line)
        return;
    last = history_get(history_length);
    if (last && strcmp(last->line, line) == 0)      /* skip immediate repeats */
        return;
    add_history(line);
}

void ui_history_load(void)
{
    char path[PATH_MAX];

    stifle_history(1000);
    if (use_readline() && history_path(path, sizeof path))
        read_history(path);
}

void ui_history_save(void)
{
    char path[PATH_MAX];

    if (use_readline() && history_path(path, sizeof path))
        write_history(path);
}

void ui_history_print(FILE *out)
{
    HIST_ENTRY **list = history_list();

    for (int i = 0; list && list[i]; i++)
        fprintf(out, "%5d  %s\n", i + history_base, list[i]->line);
}

#else  /* ---- no readline ---- */

int ui_has_history(void)
{
    return 0;
}

void ui_history_add(const char *line)
{
    (void)line;
}

void ui_history_load(void)
{
}

void ui_history_save(void)
{
}

void ui_history_print(FILE *out)
{
    (void)out;
}

#endif

static char *read_with_fgets(const char *prompt)
{
    char line[MAX_LINE];
    size_t len;

    if (prompt) {
        fputs(prompt, stdout);
        fflush(stdout);
    }
    if (!fgets(line, sizeof line, stdin)) {
        if (ferror(stdin) && errno == EINTR) {      /* Ctrl+C at the prompt */
            clearerr(stdin);
            putchar('\n');
            return xstrdup("");
        }
        if (prompt)
            putchar('\n');                            /* Ctrl+D */
        return NULL;
    }
    len = strlen(line);
    if (len && line[len - 1] == '\n') {
        line[len - 1] = '\0';
    } else if (!feof(stdin)) {
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF)
            ;
        err_msg("line too long (max %d characters)", MAX_LINE - 2);
        g_last_status = 2;
        return xstrdup("");
    }
    return xstrdup(line);
}

char *ui_read_line(const char *prompt)
{
#ifdef HAVE_READLINE
    if (prompt && use_readline()) {
        char *s = readline(prompt);

        if (!s) {
            putchar('\n');                            /* Ctrl+D */
            return NULL;
        }
        if (strlen(s) >= MAX_LINE - 1) {
            err_msg("line too long (max %d characters)", MAX_LINE - 2);
            g_last_status = 2;
            free(s);
            return xstrdup("");
        }
        return s;
    }
#endif
    return read_with_fgets(prompt);
}
