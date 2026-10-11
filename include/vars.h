#ifndef VARS_H
#define VARS_H

#include <stdio.h>

/* Shell variables. Every variable lives in the shell's own table; only
 * *exported* ones are also placed in the process environment, which is
 * what child processes inherit. That difference is the scoping rule:
 *
 *     FOO=1          shell variable  (visible to $FOO, not to children)
 *     export FOO     now also in the environment (children can see it)
 */

/* Import the process environment as exported variables. */
void vars_init(void);
void vars_free(void);

/* NULL if unset. The pointer is valid until the next var_* call. */
const char *var_get(const char *name);

/* Create or update; keeps the exported flag (and the environment) in sync. */
int var_set(const char *name, const char *value);

/* Mark exported. If value is non-NULL it is assigned first; if the variable
 * does not exist and value is NULL it is created empty. */
int var_export(const char *name, const char *value);

int var_unset(const char *name);

/* 1 if the variable exists and is exported. */
int var_is_exported(const char *name);

/* Print "NAME=value" lines sorted by name (all variables, or exported only,
 * the latter as `export NAME="value"`). */
void var_list(FILE *out, int exported_only);

/* [A-Za-z_][A-Za-z0-9_]* spanning exactly `len` bytes. */
int var_valid_name(const char *s, size_t len);

#endif
