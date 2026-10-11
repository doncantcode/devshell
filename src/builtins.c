#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "builtins.h"
#include "color.h"
#include "errors.h"
#include "fileops.h"
#include "scaffold.h"
#include "tour.h"
#include "trace.h"
#include "ui.h"
#include "vars.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

int g_running   = 1;
int g_exit_code = 0;

static int argc_of(char **argv)
{
    int n = 0;
    while (argv[n])
        n++;
    return n;
}

static int bi_cd(char **argv)
{
    const char *dir;
    int print = 0;
    char old[PATH_MAX], now[PATH_MAX];

    if (argc_of(argv) > 2) {
        err_msg("cd: too many arguments");
        return 1;
    }
    if (!argv[1]) {
        dir = var_get("HOME");
        if (!dir) {
            err_msg("cd: HOME not set");
            return 1;
        }
    } else if (strcmp(argv[1], "-") == 0) {
        dir = var_get("OLDPWD");
        if (!dir) {
            err_msg("cd: OLDPWD not set");
            return 1;
        }
        print = 1;
    } else {
        dir = argv[1];
    }

    if (!getcwd(old, sizeof old))
        old[0] = '\0';
    if (chdir(dir) < 0) {
        err_sys("cd: %s", dir);
        return 1;
    }
    if (old[0])
        var_export("OLDPWD", old);
    if (getcwd(now, sizeof now))
        var_export("PWD", now);
    if (print)
        puts(dir);
    return 0;
}

/* export                list exported variables
 * export NAME[=value]   mark (and optionally set) variables for children */
static int bi_export(char **argv)
{
    int rc = 0;

    if (!argv[1]) {
        var_list(stdout, 1);
        return 0;
    }
    for (int i = 1; argv[i]; i++) {
        const char *eq = strchr(argv[i], '=');
        size_t nlen = eq ? (size_t)(eq - argv[i]) : strlen(argv[i]);
        char *name;

        if (!var_valid_name(argv[i], nlen)) {
            err_msg("export: '%s': not a valid identifier", argv[i]);
            rc = 1;
            continue;
        }
        name = xstrdup(argv[i]);
        name[nlen] = '\0';
        var_export(name, eq ? eq + 1 : NULL);
        trace("var", "%s exported: now also in the environment inherited by children", name);
        free(name);
    }
    return rc;
}

static int bi_unset(char **argv)
{
    int rc = 0;

    for (int i = 1; argv[i]; i++) {
        if (!var_valid_name(argv[i], strlen(argv[i]))) {
            err_msg("unset: '%s': not a valid identifier", argv[i]);
            rc = 1;
            continue;
        }
        var_unset(argv[i]);
    }
    return rc;
}

/* set: list every variable, exported or not. */
static int bi_set(char **argv)
{
    (void)argv;
    var_list(stdout, 0);
    return 0;
}

static int bi_trace(char **argv)
{
    if (argv[1] && argv[2]) {
        err_msg("trace: too many arguments");
        return 1;
    }
    if (!argv[1]) {
        printf("trace is %s\n", g_trace ? "on" : "off");
        return 0;
    }
    if (strcmp(argv[1], "on") == 0) {
        g_trace = 1;
        puts("trace on: each command now shows lex, parse, expand and exec steps");
    } else if (strcmp(argv[1], "off") == 0) {
        g_trace = 0;
        puts("trace off");
    } else {
        err_msg("usage: trace [on|off]");
        return 1;
    }
    return 0;
}

static int bi_pwd(char **argv)
{
    char buf[PATH_MAX];
    (void)argv;

    if (!getcwd(buf, sizeof buf)) {
        err_sys("pwd");
        return 1;
    }
    puts(buf);
    return 0;
}

static int bi_echo(char **argv)
{
    int i = 1, newline = 1;

    if (argv[1] && strcmp(argv[1], "-n") == 0) {
        newline = 0;
        i = 2;
    }
    for (; argv[i]; i++) {
        fputs(argv[i], stdout);
        if (argv[i + 1])
            putchar(' ');
    }
    if (newline)
        putchar('\n');
    return 0;
}

static int bi_exit(char **argv)
{
    int code = g_last_status;

    if (argc_of(argv) > 2) {
        err_msg("exit: too many arguments");
        return 1;
    }
    if (argv[1]) {
        char *end;
        long v = strtol(argv[1], &end, 10);
        if (*argv[1] == '\0' || *end != '\0') {
            err_msg("exit: %s: numeric argument required", argv[1]);
            code = 2;
        } else {
            code = (int)(v & 0xFF);
        }
    }
    g_running = 0;
    g_exit_code = code;
    return code;
}

/* One row per built-in: drives both `help` and `help NAME`. */
static const struct {
    const char *group;
    const char *name;
    const char *usage;
    const char *desc;
    const char *example;
} help_tab[] = {
    { "Shell",     "cd",      "cd [DIR | -]",       "change directory; no argument goes to $HOME, '-' to the previous one", "cd ~/projects" },
    { "Shell",     "pwd",     "pwd",                "print the current directory", "pwd" },
    { "Shell",     "echo",    "echo [-n] ARG...",   "print the arguments (-n: no trailing newline)", "echo \"hello $USER\"" },
    { "Shell",     "clear",   "clear",              "clear the screen", "clear" },
    { "Shell",     "history", "history",          "list the commands you typed (needs readline)", "history" },
    { "Shell",     "help",    "help [COMMAND]",     "list commands, or explain one", "help export" },
    { "Shell",     "exit",    "exit [CODE]",        "leave the shell, optionally with an exit code", "exit 0" },
    { "Variables", "export",  "export [NAME[=VALUE]]...", "give a variable to child processes; alone, list exported ones", "export PATH=$PATH:/opt/bin" },
    { "Variables", "unset",   "unset NAME...",      "remove variables", "unset TMP" },
    { "Variables", "set",     "set",                "list every variable, exported or not", "set" },
    { "Files",     "ls",      "ls [-a] [-l] [-h] [PATH...]", "list files (-a hidden, -l long, -h human sizes); also: dir", "ls -lh src" },
    { "Files",     "dir",     "dir [-a] [-l] [-h] [PATH...]", "same as ls", "dir" },
    { "Files",     "mkdir",   "mkdir [-p] DIR...",  "create directories (-p: also parents)", "mkdir -p a/b/c" },
    { "Files",     "touch",   "touch FILE...",      "create empty files or update their timestamp", "touch notes.txt" },
    { "Files",     "cat",     "cat [FILE...]",      "print files, or standard input when none are given", "cat a.txt b.txt" },
    { "Files",     "rm",      "rm [-r] [-f] PATH...", "remove files (-r: directories too, -f: ignore missing)", "rm -r build" },
    { "Projects",  "new",     "new TYPE NAME",      "create a project from a template: c-project, python-project", "new c-project hello" },
    { "Learning",  "tour",    "tour",               "guided walkthrough: type real commands to learn pipes, redirects, variables, trace", "tour" },
    { "Learning",  "welcome", "welcome",            "show the quick-start screen again", "welcome" },
    { "Learning",  "trace",   "trace [on|off]",    "show lex, parse, expand and fork/exec/wait steps for every command", "trace on" },
};

#define NHELP (sizeof help_tab / sizeof help_tab[0])

static const char *heading(const char *s, int colour, char *buf, size_t size)
{
    if (colour)
        snprintf(buf, size, C_BBLUE "%s" C_RESET, s);
    else
        snprintf(buf, size, "%s", s);
    return buf;
}

static int bi_help(char **argv)
{
    int colour = color_ok(STDOUT_FILENO);
    char h[64];

    if (argc_of(argv) > 2) {
        err_msg("help: too many arguments");
        return 1;
    }

    if (argv[1]) {                      /* help NAME */
        for (size_t i = 0; i < NHELP; i++) {
            if (strcmp(argv[1], help_tab[i].name) != 0)
                continue;
            char h2[64], h3[64];
            printf("%s\n  %s\n\n%s\n  %s\n\n%s\n  %s\n",
                   heading("Usage", colour, h, sizeof h), help_tab[i].usage,
                   heading("What it does", colour, h2, sizeof h2), help_tab[i].desc,
                   heading("Example", colour, h3, sizeof h3), help_tab[i].example);
            return 0;
        }
        err_msg("help: no help for '%s' (external programs have their own: %s --help)",
                argv[1], argv[1]);
        return 1;
    }

    printf("%sDevShell %s%s - a small developer shell\n",
           colour ? C_BCYAN : "", DEVSHELL_VERSION, colour ? C_RESET : "");
    for (size_t i = 0; i < NHELP; i++) {
        if (i == 0 || strcmp(help_tab[i].group, help_tab[i - 1].group) != 0)
            printf("\n%s\n", heading(help_tab[i].group, colour, h, sizeof h));
        printf("  %-30s %s\n", help_tab[i].usage, help_tab[i].desc);
    }
    printf("\n%s\n"
           "  NAME=value                     set a shell variable; $NAME ${NAME} $? $$ expand\n"
           "  cmd1 | cmd2                    pipe            cmd < in    read from a file\n"
           "  cmd > out    cmd >> out        write / append  cmd &       run in the background\n"
           "  a ; b    a && b    a || b      always / if a succeeded / if a failed\n"
           "  'literal'  \"with $vars\"  \\x     quoting\n"
           "\nAnything else runs as a program from your PATH. "
           "Try %s for details on one command.\n",
           heading("Syntax", colour, h, sizeof h), "help <command>");
    return 0;
}

static int bi_welcome(char **argv)
{
    (void)argv;
    ui_banner();
    return 0;
}

static int bi_tour(char **argv)
{
    if (argv[1]) {
        err_msg("tour: takes no arguments");
        return 1;
    }
    return tour_run();
}

static int bi_history(char **argv)
{
    (void)argv;
    if (!ui_has_history()) {
        err_msg("history: not available (DevShell was built without readline)");
        return 1;
    }
    ui_history_print(stdout);
    return 0;
}

static int bi_clear(char **argv)
{
    (void)argv;
    fputs("\033[H\033[2J", stdout);
    return 0;
}

static const struct {
    const char *name;
    int (*fn)(char **argv);
} table[] = {
    { "cd",    bi_cd },
    { "pwd",   bi_pwd },
    { "echo",  bi_echo },
    { "exit",  bi_exit },
    { "help",  bi_help },
    { "history", bi_history },
    { "welcome", bi_welcome },
    { "tour",  bi_tour },
    { "clear", bi_clear },
    { "export", bi_export },
    { "unset", bi_unset },
    { "set",   bi_set },
    { "trace", bi_trace },
    { "ls",   fileops_ls },
    { "dir",   fileops_ls },
    { "mkdir", fileops_mkdir },
    { "touch", fileops_touch },
    { "cat",   fileops_cat },
    { "rm",    fileops_rm },
    { "new",   scaffold_new },
};

#define NBUILTINS (sizeof table / sizeof table[0])

int is_builtin(const Command *c)
{
    if (!c->argv[0])
        return 0;
    for (size_t i = 0; i < NBUILTINS; i++)
        if (strcmp(c->argv[0], table[i].name) == 0)
            return 1;
    return 0;
}

int run_builtin(const Command *c)
{
    for (size_t i = 0; i < NBUILTINS; i++)
        if (strcmp(c->argv[0], table[i].name) == 0)
            return table[i].fn((char **)c->argv);
    return 127;
}
