#include <stdio.h>
#include <string.h>

#include "lexer.h"

static int failures, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int word_is(const Token *t, const char *s)
{
    return t->type == TOK_WORD && strcmp(t->text, s) == 0;
}

static void test_words(void)
{
    Token t[16];
    int n;

    CHECK(lex("  ls   -l  /tmp ", t, 16, &n) == 0);
    CHECK(n == 3);
    CHECK(word_is(&t[0], "ls") && word_is(&t[1], "-l") && word_is(&t[2], "/tmp"));
    free_tokens(t, n);

    CHECK(lex("", t, 16, &n) == 0 && n == 0);
    CHECK(lex("   \t ", t, 16, &n) == 0 && n == 0);
}

static void test_operators(void)
{
    Token t[16];
    int n;

    CHECK(lex("a|b<c>d>>e&", t, 16, &n) == 0);
    CHECK(n == 10);
    CHECK(word_is(&t[0], "a") && t[1].type == TOK_PIPE);
    CHECK(word_is(&t[2], "b") && t[3].type == TOK_IN);
    CHECK(word_is(&t[4], "c") && t[5].type == TOK_OUT);
    CHECK(word_is(&t[6], "d") && t[7].type == TOK_APPEND);
    CHECK(word_is(&t[8], "e") && t[9].type == TOK_BG);
    free_tokens(t, n);
}

static void test_list_operators(void)
{
    Token t[16];
    int n;

    CHECK(lex("a;b&&c||d|e&f", t, 16, &n) == 0);
    CHECK(n == 11);
    CHECK(t[1].type == TOK_SEMI);
    CHECK(t[3].type == TOK_AND);
    CHECK(t[5].type == TOK_OR);
    CHECK(t[7].type == TOK_PIPE);
    CHECK(t[9].type == TOK_BG);
    free_tokens(t, n);

    /* a lone | or & next to a doubled one */
    CHECK(lex("a || b | c && d & e", t, 16, &n) == 0);
    CHECK(t[1].type == TOK_OR && t[3].type == TOK_PIPE &&
          t[5].type == TOK_AND && t[7].type == TOK_BG);
    free_tokens(t, n);

    /* quoted operators stay inside the word */
    CHECK(lex("echo 'a;b' \"c&&d\" e\\;f", t, 16, &n) == 0);
    CHECK(n == 4);
    free_tokens(t, n);
}

/* The lexer keeps words RAW: quotes and $ are still there. They are removed
 * and expanded later (expand.c), so they see up-to-date variables. */
static void test_raw_words(void)
{
    Token t[16];
    int n;

    CHECK(lex("echo \"a   b\" 'c | d'", t, 16, &n) == 0);
    CHECK(n == 3 && word_is(&t[1], "\"a   b\"") && word_is(&t[2], "'c | d'"));
    free_tokens(t, n);

    CHECK(lex("x\"y z\"w", t, 16, &n) == 0);
    CHECK(n == 1 && word_is(&t[0], "x\"y z\"w"));
    free_tokens(t, n);

    CHECK(lex("echo \"\" $HOME ${X}", t, 16, &n) == 0);
    CHECK(n == 4 && word_is(&t[1], "\"\"") && word_is(&t[2], "$HOME") && word_is(&t[3], "${X}"));
    free_tokens(t, n);

    CHECK(lex("echo a\\ b \\> \"q\\\"q\"", t, 16, &n) == 0);
    CHECK(n == 4 && word_is(&t[1], "a\\ b") && word_is(&t[2], "\\>") && word_is(&t[3], "\"q\\\"q\""));
    free_tokens(t, n);
}

static void test_errors(void)
{
    Token t[16];
    int n = 99;

    CHECK(lex("echo \"abc", t, 16, &n) == -1 && n == 0);
    CHECK(lex("echo 'abc", t, 16, &n) == -1 && n == 0);
    CHECK(lex("a b c d e", t, 3, &n) == -1 && n == 0);   /* token limit */
}

int main(void)
{
    test_words();
    test_operators();
    test_list_operators();
    test_raw_words();
    test_errors();
    printf("test_lexer: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
