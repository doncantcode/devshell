#ifndef SHELL_H
#define SHELL_H

#define MAX_ARGS       64  /* argv slots per command, including the NULL terminator */
#define MAX_CMDS       16  /* pipeline stages */
#define MAX_TOKENS     256
#define MAX_LIST_ITEMS 64  /* pipelines on one input line */
#define MAX_LINE       1024
#define DEVSHELL_VERSION "1.1"

typedef struct {
    char *argv[MAX_ARGS];  /* command + args, NULL-terminated, heap strings.
                              Raw source text until expand_pipeline() runs. */
    char *infile;          /* < redirect  */
    char *outfile;         /* > or >> redirect */
    int   append;          /* 1 if >> */
    int   background;      /* 1 if trailing & (set on the last stage) */
} Command;

typedef struct {
    Command cmds[MAX_CMDS];
    int     count;
} Pipeline;

/* How a pipeline in a list is joined to the one that follows it. */
typedef enum {
    CONN_SEQ,   /* ;  (or & / end of line): always run the next one */
    CONN_AND,   /* && : run the next one only if this one succeeded  */
    CONN_OR     /* || : run the next one only if this one failed     */
} Connector;

typedef struct {
    Pipeline  pipe;
    Connector next;
} ListItem;

/* One input line: pipelines joined by ; && || & */
typedef struct {
    ListItem *items;       /* heap array */
    int       count;
    int       cap;
} CommandList;

/* Exit status of the most recent command (defined in vars.c). */
extern int g_last_status;

/* REPL control, set by the `exit` built-in (defined in builtins.c). */
extern int g_running;
extern int g_exit_code;

/* 1 when reading from a terminal (or started with -i); set in main.c. */
extern int g_interactive;

/* Run one input line through lex -> parse -> expand -> execute (main.c). */
void shell_run_line(const char *line);

#endif
