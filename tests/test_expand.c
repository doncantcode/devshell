#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "expand.h"
#include "parser.h"
#include "vars.h"

static int failures, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* expand `raw` and compare with `want` */
static int ex(const char *raw, const char *want)
{
    char *got = expand_word(raw, NULL);
    int ok = strcmp(got, want) == 0;

    if (!ok)
        fprintf(stderr, "  expand(%s) = \"%s\", wanted \"%s\"\n", raw, got, want);
    free(got);
    return ok;
}

static void test_quotes(void)
{
    CHECK(ex("plain", "plain"));
    CHECK(ex("\"a   b\"", "a   b"));
    CHECK(ex("'c | d'", "c | d"));
    CHECK(ex("x\"y z\"w", "xy zw"));
    CHECK(ex("\"\"", ""));
    CHECK(ex("a\\ b", "a b"));
    CHECK(ex("\\>", ">"));
    CHECK(ex("\"q\\\"q\"", "q\"q"));
    CHECK(ex("'it'\"s\"", "its"));
}

static void test_variables(void)
{
    vars_init();
    var_set("NAME", "world");
    var_set("EMPTY", "");

    CHECK(ex("$NAME", "world"));
    CHECK(ex("${NAME}", "world"));
    CHECK(ex("hello-$NAME!", "hello-world!"));
    CHECK(ex("${NAME}s", "worlds"));
    CHECK(ex("$NAMEs", ""));                    /* variable "NAMEs" is unset */
    CHECK(ex("$NOPE", ""));
    CHECK(ex("a${NOPE}b", "ab"));
    CHECK(ex("$NAME$NAME", "worldworld"));

    /* quoting rules */
    CHECK(ex("\"$NAME\"", "world"));
    CHECK(ex("\"hi $NAME\"", "hi world"));
    CHECK(ex("'$NAME'", "$NAME"));
    CHECK(ex("\\$NAME", "$NAME"));
    CHECK(ex("\"\\$NAME\"", "$NAME"));

    /* literal $ when nothing follows that can be a name */
    CHECK(ex("$", "$"));
    CHECK(ex("$-", "$-"));
    CHECK(ex("5$", "5$"));
    CHECK(ex("${", "${"));
    CHECK(ex("${bad name}", "${bad name}"));
}

static void test_special(void)
{
    char want[32];

    g_last_status = 7;
    CHECK(ex("$?", "7"));
    CHECK(ex("code=$?", "code=7"));
    g_last_status = 0;
    CHECK(ex("\"$?\"", "0"));

    snprintf(want, sizeof want, "%d", (int)getpid());
    CHECK(ex("$$", want));
}

static void test_removal(void)
{
    int removed = 0;
    char *s;

    var_set("EMPTY", "");

    s = expand_word("$EMPTY", &removed);
    CHECK(removed == 1);
    free(s);

    s = expand_word("\"$EMPTY\"", &removed);       /* quoted: kept as "" */
    CHECK(removed == 0 && *s == '\0');
    free(s);

    s = expand_word("\"\"", &removed);
    CHECK(removed == 0);
    free(s);

    s = expand_word("x$EMPTY", &removed);
    CHECK(removed == 0 && strcmp(s, "x") == 0);
    free(s);
}

static void test_scoping(void)
{
    const char *v;

    var_set("LOCAL", "1");
    CHECK(!var_is_exported("LOCAL"));
    CHECK(getenv("LOCAL") == NULL);              /* not visible to children */

    var_export("LOCAL", NULL);
    CHECK(var_is_exported("LOCAL"));
    CHECK(getenv("LOCAL") && strcmp(getenv("LOCAL"), "1") == 0);

    var_set("LOCAL", "2");                       /* update keeps it exported */
    CHECK(strcmp(getenv("LOCAL"), "2") == 0);

    var_export("NEWVAR", "x");                   /* export NAME=value */
    CHECK(strcmp(getenv("NEWVAR"), "x") == 0);

    var_unset("LOCAL");
    CHECK(var_get("LOCAL") == NULL && getenv("LOCAL") == NULL);

    v = var_get("PATH");                         /* imported from the environment */
    CHECK(v != NULL && var_is_exported("PATH"));

    CHECK(var_set("bad-name", "x") == -1);
    CHECK(var_valid_name("_a1", 3) && !var_valid_name("1a", 2) && !var_valid_name("", 0));
}

static void test_pipeline_expansion(void)
{
    CommandList l;
    Pipeline *p;

    var_set("FILE", "out.txt");
    var_set("EMPTY", "");

    CHECK(lex_and_parse("echo \"a b\" $EMPTY $FILE > $FILE", &l) == 0);
    p = &l.items[0].pipe;
    CHECK(expand_pipeline(p) == 0);
    CHECK(strcmp(p->cmds[0].argv[0], "echo") == 0);
    CHECK(strcmp(p->cmds[0].argv[1], "a b") == 0);
    CHECK(strcmp(p->cmds[0].argv[2], "out.txt") == 0);   /* $EMPTY was removed */
    CHECK(p->cmds[0].argv[3] == NULL);
    CHECK(strcmp(p->cmds[0].outfile, "out.txt") == 0);
    free_list(&l);

    CHECK(lex_and_parse("echo hi > $EMPTY", &l) == 0);
    CHECK(expand_pipeline(&l.items[0].pipe) == -1);      /* ambiguous redirect */
    free_list(&l);

    CHECK(is_assignment("A=1") && is_assignment("_x=") && is_assignment("A=b=c"));
    CHECK(!is_assignment("=1") && !is_assignment("1A=1") && !is_assignment("a-b=1"));
    CHECK(!is_assignment("plain") && !is_assignment("\"A\"=1"));
}

int main(void)
{
    test_quotes();
    test_variables();
    test_special();
    test_removal();
    test_scoping();
    test_pipeline_expansion();
    vars_free();
    printf("test_expand: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
