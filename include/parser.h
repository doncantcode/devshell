#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "shell.h"

/* Build a CommandList from tokens (strings are copied).
 * Returns 0 on success, 1 if there were no tokens (empty input),
 * -1 on a syntax error (message printed, list left empty). */
int parse(const Token *toks, int n, CommandList *l);

/* lex + parse in one step; same return codes as parse(). */
int lex_and_parse(const char *line, CommandList *l);

void free_pipeline(Pipeline *p);
void free_list(CommandList *l);

#endif
