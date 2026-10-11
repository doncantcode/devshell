#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "shell.h"

/* Run one pipeline: expand its words, fork/exec each stage, wire pipes and
 * redirections, wait for foreground jobs. Returns (and stores in
 * g_last_status) the exit status of the last stage. */
int execute_pipeline(Pipeline *p);

/* Run a whole input line, honouring ; && || between pipelines. */
int execute_list(CommandList *l);

/* Collect finished background children without blocking. */
void reap_background(void);

#endif
