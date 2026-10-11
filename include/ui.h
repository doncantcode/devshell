#ifndef UI_H
#define UI_H

#include <stddef.h>
#include <stdio.h>

/* The interactive front end: banner, prompt, line reading and history.
 * Everything is optional-feature safe: without readline (not installed at
 * build time) the shell falls back to plain fgets() and has no history. */

/* Startup quick-start screen (also the `welcome` command). */
void ui_banner(void);

/* A short secondary prompt such as "tour> ", in the given ANSI colour. */
void ui_simple_prompt(char *buf, size_t size, const char *text, const char *colour);

/* Fill `buf` with the prompt:  devshell ~/proj/app (main) [1]>
 * (cwd, git branch if inside a repository, last exit status when non-zero).
 * Coloured only when stdout allows it. */
void ui_build_prompt(char *buf, size_t size);

/* Read one line without its newline, as a malloc'd string.
 *   NULL        end of input (Ctrl+D / EOF)
 *   ""          nothing to do (blank line, Ctrl+C, or an over-long line,
 *               in which case a message has already been printed)
 * `prompt` may be NULL to read silently (scripts, pipes). */
char *ui_read_line(const char *prompt);

int  ui_has_history(void);
void ui_history_add(const char *line);
void ui_history_load(void);
void ui_history_save(void);
void ui_history_print(FILE *out);

#endif
