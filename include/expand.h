#ifndef EXPAND_H
#define EXPAND_H

#include "shell.h"

/* Expansion phase: turns raw words into final argument strings.
 *
 *   $NAME  ${NAME}   variable value ("" if unset)
 *   $?               exit status of the previous command
 *   $$               the shell's process id
 *   '...'            literal text (no expansion)
 *   "..."            literal text, but $ expansions still happen
 *   \c               the character c, literally
 *
 * There is no word splitting or globbing: one raw word is one argument.
 * Returns a heap string. *removed (may be NULL) is set to 1 when the word
 * was unquoted and expanded to nothing, in which case callers drop it. */
char *expand_word(const char *raw, int *removed);

/* Expand argv[] and redirect targets of every stage, in place.
 * Returns 0, or -1 (message printed) e.g. for an empty redirect target. */
int expand_pipeline(Pipeline *p);

/* 1 if `raw` looks like NAME=value with a valid NAME. */
int is_assignment(const char *raw);

#endif
