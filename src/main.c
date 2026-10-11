#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "errors.h"
#include "executor.h"
#include "parser.h"
#include "shell.h"
#include "trace.h"
#include "ui.h"
#include "vars.h"

int g_interactive = 0;

/* Ctrl+C: only interrupt the read at the prompt / the wait for a child.
 * Installed without SA_RESTART so fgets() returns with EINTR. */
static void on_sigint(int sig)
{
    (void)sig;
}

static void install_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
}

/* One input line through the whole pipeline of phases:
 *   lex -> parse -> (per pipeline) expand -> execute */
void shell_run_line(const char *line)
{
    Token toks[MAX_TOKENS];
    CommandList list;
    int n = 0, rc;

    if (lex(line, toks, MAX_TOKENS, &n) < 0) {
        g_last_status = 2;
        return;
    }
    trace_tokens(toks, n);

    rc = parse(toks, n, &list);
    free_tokens(toks, n);
    if (rc < 0) {
        g_last_status = 2;
        return;
    }
    if (rc > 0)
        return;                         /* empty line */

    trace_list(&list);
    execute_list(&list);
    free_list(&list);
}

/* Options:  -i  act interactively even if stdin is not a terminal
 *           -q  no startup banner (also: DEVSHELL_QUIET=1)             */
int main(int argc, char **argv)
{
    int force_interactive = 0, quiet = 0, status;
    const char *env_quiet = getenv("DEVSHELL_QUIET");

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0) {
            force_interactive = 1;
        } else if (strcmp(argv[i], "-q") == 0) {
            quiet = 1;
        } else {
            fprintf(stderr, "usage: %s [-i] [-q]\n", argv[0]);
            return 2;
        }
    }
    if (env_quiet && *env_quiet && strcmp(env_quiet, "0") != 0)
        quiet = 1;
    g_interactive = isatty(STDIN_FILENO) || force_interactive;

    install_signals();
    vars_init();
    if (g_interactive) {
        ui_history_load();
        if (!quiet)
            ui_banner();
    }

    while (g_running) {
        char prompt[512], *line;

        reap_background();
        if (g_interactive)
            ui_build_prompt(prompt, sizeof prompt);

        line = ui_read_line(g_interactive ? prompt : NULL);
        if (!line)
            break;                      /* end of input */

        if (*line) {
            if (g_interactive)
                ui_history_add(line);
            shell_run_line(line);
        }
        free(line);
    }

    if (g_interactive)
        ui_history_save();
    status = g_running ? g_last_status : g_exit_code;
    vars_free();
    return status;
}
