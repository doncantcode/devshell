#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "color.h"
#include "errors.h"
#include "fileops.h"
#include "shell.h"
#include "tour.h"
#include "trace.h"
#include "ui.h"
#include "vars.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* ----------------------------------------------------- line predicates */

static const char *skip_space(const char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    return s;
}

/* Does the first word of `line` equal `word`? */
static int first_word_is(const char *line, const char *word)
{
    size_t n = strlen(word);

    line = skip_space(line);
    return strncmp(line, word, n) == 0 &&
           (line[n] == '\0' || isspace((unsigned char)line[n]) || line[n] == ';');
}

static int has_pipe(const char *line)
{
    for (const char *p = line; *p; p++) {
        if (*p == '|' && p[1] == '|') {
            p++;
            continue;
        }
        if (*p == '|')
            return 1;
    }
    return 0;
}

/* NAME=value at the start of the line. */
static int starts_with_assignment(const char *line)
{
    const char *p = skip_space(line), *eq = strchr(p, '=');
    return eq && var_valid_name(p, (size_t)(eq - p));
}

static int is_exactly(const char *line, const char *text)
{
    line = skip_space(line);
    size_t n = strlen(text);
    return strncmp(line, text, n) == 0 && *skip_space(line + n) == '\0';
}

static int chk_echo(const char *l)     { return first_word_is(l, "echo"); }
static int chk_ls(const char *l)       { return first_word_is(l, "ls") || first_word_is(l, "dir"); }
static int chk_pipe(const char *l)     { return has_pipe(l); }
static int chk_redirect(const char *l) { return strchr(l, '>') != NULL; }
static int chk_cat(const char *l)      { return first_word_is(l, "cat"); }
static int chk_assign(const char *l)   { return starts_with_assignment(l); }
static int chk_expand(const char *l)   { return strchr(l, '$') != NULL; }
static int chk_list(const char *l)     { return strstr(l, "&&") || strstr(l, "||") || strchr(l, ';'); }
static int chk_trace_on(const char *l) { return is_exactly(l, "trace on"); }
static int chk_any(const char *l)      { return *skip_space(l) && !first_word_is(l, "trace"); }
static int chk_trace_off(const char *l){ return is_exactly(l, "trace off"); }

/* ------------------------------------------------------------- steps */

typedef struct {
    const char *title;
    const char *explain;     /* printed before the prompt; may contain \n */
    const char *example;     /* what we suggest typing */
    const char *learned;     /* printed after a matching command */
    int (*check)(const char *line);
} Step;

static const Step steps[] = {
    { "Run a command",
      "A shell reads a line, splits it into words, and runs the first word\n"
      "as a command with the rest as arguments.",
      "echo hello",
      "echo is a built-in: it ran inside the shell itself, with no new process.",
      chk_echo },
    { "Look around",
      "Not every command is a built-in. ls and dir list files; here the\n"
      "tour is working in a scratch folder that is deleted when you finish.",
      "ls",
      "That folder is empty for now. Next we will put something in it.",
      chk_ls },
    { "Connect commands with a pipe",
      "| sends the output of the left command to the input of the right one.\n"
      "The shell starts both processes and joins them with pipe() and dup2().",
      "echo hello | tr a-z A-Z",
      "Two processes ran at once, joined by the pipe. Try 'trace on' later to see it.",
      chk_pipe },
    { "Send output to a file",
      "> writes output to a file instead of the screen (>> appends).",
      "echo hi > note.txt",
      "Nothing printed: the output went into note.txt.",
      chk_redirect },
    { "Read it back",
      "cat prints a file. You just created note.txt in the scratch folder.",
      "cat note.txt",
      "Redirection and files work as expected.",
      chk_cat },
    { "Set a variable",
      "NAME=value makes a shell variable. There must be no spaces around =.",
      "X=5",
      "Stored. It lives in the shell and is not yet visible to other programs;\n"
      "'export X' would hand it to them.",
      chk_assign },
    { "Use a variable",
      "$NAME is replaced by its value just before a command runs. Single\n"
      "quotes stop that: '$X' stays as it is.",
      "echo $X",
      "That replacement is the expansion phase, between parsing and execution.",
      chk_expand },
    { "Chain commands",
      "a && b runs b only if a succeeded; a || b only if a failed; a ; b always.",
      "false || echo recovered",
      "The exit status of each command decides what runs next.",
      chk_list },
    { "Turn on trace mode",
      "trace on makes the shell narrate every step it takes for each command.",
      "trace on",
      "Trace is on. From now on every command prints its lex, parse, expand\n"
      "and fork/exec steps (colour-coded) as well as its output.",
      chk_trace_on },
    { "Read a trace",
      "Run any command and look at the trace lines above its output:\n"
      "[lex] tokens, [parse] structure, [expand] substitutions, [exec] fork/pipe/exec.",
      "echo hi | tr a-z A-Z",
      "Each [exec] fork() line is a real new process; [wait] is the shell collecting it.",
      chk_any },
    { "Turn trace off",
      "That is enough narration.",
      "trace off",
      "Back to normal.",
      chk_trace_off },
};

#define NSTEPS ((int)(sizeof steps / sizeof steps[0]))

/* ------------------------------------------------------------ driver */

static char *trim(char *s)
{
    char *end;

    while (isspace((unsigned char)*s))
        s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

static void say(const char *colour, const char *fmt, ...) PRINTF_FMT(2, 3);

static void say(const char *colour, const char *fmt, ...)
{
    va_list ap;
    int c = colour && color_ok(1);

    if (c)
        fputs(colour, stdout);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (c)
        fputs(C_RESET, stdout);
}

int tour_run(void)
{
    char orig[PATH_MAX], scratch[PATH_MAX], prompt[64];
    const char *tmp = var_get("TMPDIR");
    int saved_trace = g_trace, quit = 0, var_was_unset = 0;
    char var_name[128] = "";

    if (!g_interactive) {
        err_msg("tour: needs an interactive session (run ./devshell in a terminal)");
        return 1;
    }
    if (!getcwd(orig, sizeof orig)) {
        err_sys("tour: getcwd");
        return 1;
    }
    snprintf(scratch, sizeof scratch, "%s/devshell-tour-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(scratch)) {
        err_sys("tour: cannot create a scratch folder");
        return 1;
    }
    if (chdir(scratch) < 0) {
        err_sys("tour: %s", scratch);
        return 1;
    }
    ui_simple_prompt(prompt, sizeof prompt, "tour", C_YELLOW);

    say(C_BCYAN, "\nDevShell tour");
    printf(" - %d short steps.\n", NSTEPS);
    printf("Type the suggested command (or your own variation). It really runs.\n"
           "Say 'skip' to move on or 'quit' to leave. Nothing here touches your files.\n");

    for (int i = 0; i < NSTEPS && !quit && g_running; i++) {
        const Step *st = &steps[i];

        say(C_BBLUE, "\nStep %d of %d: %s\n", i + 1, NSTEPS, st->title);
        printf("%s\n", st->explain);
        say(C_DIM, "Try: ");
        say(C_CYAN, "%s\n", st->example);

        for (;;) {
            char *raw = ui_read_line(prompt), *line;
            int matched;

            if (!raw) {                          /* Ctrl+D */
                quit = 1;
                break;
            }
            line = trim(raw);
            if (!*line) {                        /* blank line or Ctrl+C */
                free(raw);
                continue;
            }
            if (strcmp(line, "quit") == 0 || strcmp(line, "q") == 0) {
                free(raw);
                quit = 1;
                break;
            }
            if (strcmp(line, "skip") == 0 || strcmp(line, "s") == 0) {
                free(raw);
                break;
            }

            if (starts_with_assignment(line) && !var_name[0]) {
                const char *eq = strchr(line, '=');
                size_t n = (size_t)(eq - line);
                if (n < sizeof var_name) {
                    memcpy(var_name, line, n);
                    var_name[n] = '\0';
                    var_was_unset = var_get(var_name) == NULL;
                }
            }

            shell_run_line(line);
            matched = st->check(line);
            free(raw);

            if (!g_running) {                    /* user typed `exit` */
                quit = 1;
                break;
            }
            if (matched) {
                say(C_GREEN, "[ok] ");
                printf("%s\n", st->learned);
                break;
            }
            say(C_YELLOW, "That ran, but this step is about \"%s\". Try: %s   (or 'skip')\n",
                st->title, st->example);
        }
    }

    if (!quit) {
        say(C_BCYAN, "\nTour complete.\n");
        printf("Where next:  help <command> for details, 'trace on' to watch anything you\n"
               "run, and docs/grammar.md for how lex, parse, expand and exec fit together.\n");
    } else {
        printf("\nLeaving the tour. Run 'tour' again any time.\n");
    }

    /* put everything back the way it was */
    g_trace = saved_trace;
    if (var_name[0] && var_was_unset)
        var_unset(var_name);
    if (chdir(orig) < 0)
        err_sys("tour: cannot return to %s", orig);
    else
        var_export("PWD", orig);
    {
        char *rm_argv[] = { "rm", "-r", "-f", scratch, NULL };
        fileops_rm(rm_argv);
    }
    g_last_status = 0;
    return 0;
}
