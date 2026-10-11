#ifndef TRACE_H
#define TRACE_H

#include "errors.h"
#include "lexer.h"
#include "shell.h"

/* 1 while `trace on` is active. */
extern int g_trace;

/* Print "[tag]  message" to stderr when tracing; no-op otherwise.
 * Tags used: lex, parse, expand, var, exec, wait, reap. */
void trace(const char *tag, const char *fmt, ...) PRINTF_FMT(2, 3);

void trace_tokens(const Token *toks, int n);
void trace_list(const CommandList *l);

#endif
