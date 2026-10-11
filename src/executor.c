#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "builtins.h"
#include "errors.h"
#include "executor.h"
#include "expand.h"
#include "trace.h"
#include "vars.h"

/* Open `path` and make it `target` (0 or 1). Returns 0 or -1 (message printed). */
static int redirect_fd(const char *path, int flags, int target)
{
    int fd = open(path, flags, 0644);

    if (fd < 0) {
        err_sys("%s", path);
        return -1;
    }
    if (fd != target) {
        if (dup2(fd, target) < 0) {
            err_sys("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

static int apply_redirects(const Command *c)
{
    if (c->infile && redirect_fd(c->infile, O_RDONLY, STDIN_FILENO) < 0)
        return -1;
    if (c->outfile) {
        int flags = O_WRONLY | O_CREAT | (c->append ? O_APPEND : O_TRUNC);
        if (redirect_fd(c->outfile, flags, STDOUT_FILENO) < 0)
            return -1;
    }
    return 0;
}

/* A lone built-in runs in the shell process (so `cd` works); redirections
 * are applied around it and undone afterwards. */
static int run_builtin_in_parent(const Command *c)
{
    int saved_in = -1, saved_out = -1, status;

    fflush(stdout);
    if (c->infile)
        saved_in = dup(STDIN_FILENO);
    if (c->outfile)
        saved_out = dup(STDOUT_FILENO);

    if (apply_redirects(c) < 0)
        status = 1;
    else
        status = run_builtin(c);

    fflush(stdout);
    if (saved_in >= 0) {
        dup2(saved_in, STDIN_FILENO);
        close(saved_in);
    }
    if (saved_out >= 0) {
        dup2(saved_out, STDOUT_FILENO);
        close(saved_out);
    }
    return status;
}

/* Runs in the forked child; never returns. */
static void run_child(const Command *c, int index, int in_fd, const int out[2], int has_out, int bg)
{
    signal(SIGINT, bg ? SIG_IGN : SIG_DFL);

    if (in_fd >= 0) {
        dup2(in_fd, STDIN_FILENO);
        close(in_fd);
    } else if (bg && index == 0 && !c->infile) {
        int nul = open("/dev/null", O_RDONLY);
        if (nul >= 0) {
            dup2(nul, STDIN_FILENO);
            close(nul);
        }
    }
    if (has_out) {
        dup2(out[1], STDOUT_FILENO);
        close(out[0]);
        close(out[1]);
    }
    if (apply_redirects(c) < 0)
        _exit(1);

    if (!c->argv[0])                    /* stage expanded to nothing */
        _exit(0);

    if (is_builtin(c)) {
        int status = run_builtin(c);
        fflush(stdout);
        _exit(status);
    }

    execvp(c->argv[0], c->argv);
    if (errno == ENOENT)
        err_msg("%s: command not found", c->argv[0]);
    else
        err_sys("%s", c->argv[0]);
    _exit(errno == ENOENT ? 127 : 126);
}

static int wait_for(pid_t pid)
{
    int st, code = 1;

    while (waitpid(pid, &st, 0) < 0) {
        if (errno == EINTR)
            continue;
        err_sys("waitpid");
        return 1;
    }
    if (WIFEXITED(st)) {
        code = WEXITSTATUS(st);
        trace("wait", "waitpid(%d) -> exited with status %d", (int)pid, code);
    } else if (WIFSIGNALED(st)) {
        int sig = WTERMSIG(st);
        trace("wait", "waitpid(%d) -> killed by signal %d", (int)pid, sig);
        if (sig == SIGINT)
            putchar('\n');
        else if (sig != SIGPIPE)
            fprintf(stderr, "%s\n", strsignal(sig));
        code = 128 + sig;
    }
    return code;
}

/* ------------------------------------------------------------ tracing */

static void appendf(char *buf, size_t size, const char *fmt, ...)
{
    size_t len = strlen(buf);
    va_list ap;

    if (len >= size - 1)
        return;
    va_start(ap, fmt);
    vsnprintf(buf + len, size - len, fmt, ap);
    va_end(ap);
}

/* Describe what the child is about to do (the parent prints it, because
 * output printed in the child after dup2() would land in the redirect). */
static void trace_child(const Command *c, pid_t pid, int index, int in_fd,
                        const int out[2], int has_out, int bg)
{
    char how[512] = "";

    if (!g_trace)
        return;
    trace("exec", "fork() -> pid %d  (stage %d: %s)", (int)pid, index,
          c->argv[0] ? c->argv[0] : "(empty)");

    if (c->infile)
        appendf(how, sizeof how, "open(\"%s\") -> stdin; ", c->infile);
    else if (in_fd >= 0)
        appendf(how, sizeof how, "dup2(pipe fd %d, 0); ", in_fd);
    else if (bg && index == 0)
        appendf(how, sizeof how, "open(\"/dev/null\") -> stdin; ");

    if (c->outfile)
        appendf(how, sizeof how, "open(\"%s\", %s) -> stdout; ", c->outfile,
                c->append ? "append" : "truncate");
    else if (has_out)
        appendf(how, sizeof how, "dup2(pipe fd %d, 1); ", out[1]);

    if (!c->argv[0])
        appendf(how, sizeof how, "nothing to run");
    else if (is_builtin(c))
        appendf(how, sizeof how, "run built-in '%s' inside the child", c->argv[0]);
    else
        appendf(how, sizeof how, "execvp(\"%s\")", c->argv[0]);
    trace("exec", "    child %d: %s", (int)pid, how);
}

/* ------------------------------------------------------- assignments */

static int all_assignments(const Command *c)
{
    if (!c->argv[0])
        return 0;
    for (int i = 0; c->argv[i]; i++)
        if (!is_assignment(c->argv[i]))
            return 0;
    return 1;
}

/* NAME=value [NAME2=value2 ...] as a command of its own. */
static int run_assignments(const Command *c)
{
    for (int i = 0; c->argv[i]; i++) {
        const char *raw = c->argv[i];
        const char *eq = strchr(raw, '=');
        char *name = xstrdup(raw);
        char *value;

        name[eq - raw] = '\0';
        value = expand_word(eq + 1, NULL);
        if (g_trace && strcmp(eq + 1, value) != 0)
            trace("expand", "%s -> %s", eq + 1, value);
        var_set(name, value);
        trace("var", "%s=%s  (%s)", name, value,
              var_is_exported(name) ? "exported: also in the environment"
                                    : "shell variable: not visible to child processes");
        free(name);
        free(value);
    }
    g_last_status = 0;
    return 0;
}

/* ---------------------------------------------------------- pipelines */

int execute_pipeline(Pipeline *p)
{
    int n = p->count;
    int bg = p->cmds[n - 1].background;
    pid_t pids[MAX_CMDS];
    int started = 0, prev_read = -1;

    if (n == 1 && all_assignments(&p->cmds[0]))
        return run_assignments(&p->cmds[0]);

    if (expand_pipeline(p) < 0) {
        g_last_status = 1;
        return 1;
    }
    if (n == 1 && !p->cmds[0].argv[0]) {      /* e.g. a lone $UNSET */
        g_last_status = 0;
        return 0;
    }

    if (n == 1 && is_builtin(&p->cmds[0])) {
        trace("exec", "'%s' is a built-in: runs inside the shell process, no fork()",
              p->cmds[0].argv[0]);
        g_last_status = run_builtin_in_parent(&p->cmds[0]);
        trace("exec", "exit status = %d", g_last_status);
        return g_last_status;
    }

    fflush(stdout);
    fflush(stderr);

    for (int i = 0; i < n; i++) {
        int fd[2] = { -1, -1 };
        int has_out = (i < n - 1);
        pid_t pid;

        if (has_out && pipe(fd) < 0) {
            err_sys("pipe");
            break;
        }
        if (has_out)
            trace("exec", "pipe() -> read fd %d, write fd %d", fd[0], fd[1]);
        pid = fork();
        if (pid < 0) {
            err_sys("fork");
            if (has_out) {
                close(fd[0]);
                close(fd[1]);
            }
            break;
        }
        if (pid == 0)
            run_child(&p->cmds[i], i, prev_read, fd, has_out, bg);

        trace_child(&p->cmds[i], pid, i, prev_read, fd, has_out, bg);
        pids[started++] = pid;
        if (prev_read >= 0)
            close(prev_read);
        prev_read = -1;
        if (has_out) {
            close(fd[1]);          /* parent keeps only the read end for the next stage */
            prev_read = fd[0];
        }
    }
    if (prev_read >= 0)
        close(prev_read);

    if (started < n) {
        /* pipe()/fork() failed part-way: clean up what we started. */
        for (int i = 0; i < started; i++)
            wait_for(pids[i]);
        g_last_status = 1;
        return g_last_status;
    }

    if (bg) {
        trace("exec", "background job: the shell does not wait");
        fprintf(stderr, "[bg] %d\n", (int)pids[started - 1]);
        g_last_status = 0;
        return 0;
    }

    int status = 0;
    for (int i = 0; i < started; i++)
        status = wait_for(pids[i]);     /* status of the last stage wins */
    g_last_status = status;
    trace("exec", "exit status = %d", status);
    return status;
}

int execute_list(CommandList *l)
{
    for (int i = 0; i < l->count && g_running; i++) {
        if (i > 0) {
            Connector c = l->items[i - 1].next;
            int skip = (c == CONN_AND && g_last_status != 0) ||
                       (c == CONN_OR && g_last_status == 0);

            if (c != CONN_SEQ)
                trace("exec", "'%s' after status %d: pipeline #%d %s",
                      c == CONN_AND ? "&&" : "||", g_last_status, i + 1,
                      skip ? "is skipped" : "runs");
            if (skip)
                continue;
        }
        if (l->count > 1)
            trace("exec", "-- pipeline #%d --", i + 1);
        execute_pipeline(&l->items[i].pipe);
    }
    return g_last_status;
}

/* Called between prompts only, so it can never steal a foreground child
 * from wait_for(). */
void reap_background(void)
{
    int st;
    pid_t pid;

    while ((pid = waitpid(-1, &st, WNOHANG)) > 0) {
        int code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
        trace("reap", "waitpid(-1, WNOHANG) -> pid %d finished", (int)pid);
        fprintf(stderr, "[done] %d (status %d)\n", (int)pid, code);
    }
}
