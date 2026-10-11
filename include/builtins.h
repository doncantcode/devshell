#ifndef BUILTINS_H
#define BUILTINS_H

#include "shell.h"

/* Is argv[0] of this command a built-in (cd, pwd, echo, ls, new, ...)? */
int is_builtin(const Command *c);

/* Run a built-in in the current process; returns its exit status.
 * Redirections are NOT applied here (the executor does that). */
int run_builtin(const Command *c);

#endif
