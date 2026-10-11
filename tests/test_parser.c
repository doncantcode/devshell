#include <stdio.h>
#include <string.h>

#include "parser.h"

static int failures, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void test_simple(void)
{
    CommandList l;
    Pipeline *p;

    CHECK(lex_and_parse("ls -l /tmp", &l) == 0);
    CHECK(l.count == 1);
    p = &l.items[0].pipe;
    CHECK(p->count == 1);
    CHECK(strcmp(p->cmds[0].argv[0], "ls") == 0);
    CHECK(strcmp(p->cmds[0].argv[2], "/tmp") == 0);
    CHECK(p->cmds[0].argv[3] == NULL);
    CHECK(!p->cmds[0].infile && !p->cmds[0].outfile && !p->cmds[0].background);
    free_list(&l);

    CHECK(lex_and_parse("", &l) == 1);
    CHECK(lex_and_parse("    ", &l) == 1);
}

static void test_pipeline_and_redirects(void)
{
    CommandList l;
    Pipeline *p;

    CHECK(lex_and_parse("cat < in.txt | grep x | wc -l >> out.txt &", &l) == 0);
    CHECK(l.count == 1);
    p = &l.items[0].pipe;
    CHECK(p->count == 3);
    CHECK(strcmp(p->cmds[0].infile, "in.txt") == 0);
    CHECK(strcmp(p->cmds[1].argv[1], "x") == 0);
    CHECK(strcmp(p->cmds[2].outfile, "out.txt") == 0 && p->cmds[2].append == 1);
    CHECK(p->cmds[2].background == 1 && p->cmds[0].background == 0);
    free_list(&l);

    CHECK(lex_and_parse("echo hi > a.txt", &l) == 0);
    p = &l.items[0].pipe;
    CHECK(strcmp(p->cmds[0].outfile, "a.txt") == 0 && p->cmds[0].append == 0);
    free_list(&l);

    /* the parser keeps words raw; quoted operators are still one word */
    CHECK(lex_and_parse("echo '|' \">\"", &l) == 0);
    p = &l.items[0].pipe;
    CHECK(l.count == 1 && p->count == 1 && strcmp(p->cmds[0].argv[1], "'|'") == 0);
    free_list(&l);
}

static void test_lists(void)
{
    CommandList l;

    CHECK(lex_and_parse("a ; b && c || d", &l) == 0);
    CHECK(l.count == 4);
    CHECK(l.items[0].next == CONN_SEQ);
    CHECK(l.items[1].next == CONN_AND);
    CHECK(l.items[2].next == CONN_OR);
    CHECK(strcmp(l.items[3].pipe.cmds[0].argv[0], "d") == 0);
    free_list(&l);

    /* pipes bind tighter than && */
    CHECK(lex_and_parse("a | b && c | d", &l) == 0);
    CHECK(l.count == 2 && l.items[0].pipe.count == 2 && l.items[1].pipe.count == 2);
    CHECK(l.items[0].next == CONN_AND);
    free_list(&l);

    /* & separates, and only backgrounds its own pipeline */
    CHECK(lex_and_parse("sleep 1 & echo hi", &l) == 0);
    CHECK(l.count == 2);
    CHECK(l.items[0].pipe.cmds[0].background == 1);
    CHECK(l.items[1].pipe.cmds[0].background == 0);
    CHECK(l.items[0].next == CONN_SEQ);
    free_list(&l);

    /* trailing ; and & are fine */
    CHECK(lex_and_parse("echo hi ;", &l) == 0 && l.count == 1);
    free_list(&l);
    CHECK(lex_and_parse("echo hi &", &l) == 0 && l.count == 1);
    CHECK(l.items[0].pipe.cmds[0].background == 1);
    free_list(&l);

    CHECK(lex_and_parse("a ; b ; c ; d ; e", &l) == 0 && l.count == 5);
    free_list(&l);
}

static void test_syntax_errors(void)
{
    CommandList l;

    CHECK(lex_and_parse("| ls", &l) == -1);
    CHECK(lex_and_parse("ls |", &l) == -1);
    CHECK(lex_and_parse("ls | | wc", &l) == -1);
    CHECK(lex_and_parse("ls >", &l) == -1);
    CHECK(lex_and_parse("ls > | wc", &l) == -1);
    CHECK(lex_and_parse("&", &l) == -1);
    CHECK(lex_and_parse("echo \"oops", &l) == -1);
    CHECK(l.count == 0 && l.items == NULL);

    CHECK(lex_and_parse("; ls", &l) == -1);
    CHECK(lex_and_parse("ls ;; ls", &l) == -1);
    CHECK(lex_and_parse("ls &&", &l) == -1);
    CHECK(lex_and_parse("&& ls", &l) == -1);
    CHECK(lex_and_parse("ls || || ls", &l) == -1);
    CHECK(lex_and_parse("ls && ; ls", &l) == -1);
    CHECK(lex_and_parse("ls && ls &", &l) == -1);     /* & after && list */
    CHECK(lex_and_parse("ls ; > f", &l) == -1);
}

static void test_limits(void)
{
    CommandList l;
    char line[4096] = "echo";

    for (int i = 0; i < MAX_ARGS; i++)
        strcat(line, " x");
    CHECK(lex_and_parse(line, &l) == -1);

    line[0] = '\0';
    for (int i = 0; i < MAX_CMDS + 1; i++)
        strcat(line, i ? " | cat" : "cat");
    CHECK(lex_and_parse(line, &l) == -1);

    line[0] = '\0';
    for (int i = 0; i < 40; i++)           /* 40 * 2 + ... tokens stays < 256 */
        strcat(line, i ? " ; ls" : "ls");
    CHECK(lex_and_parse(line, &l) == 0 && l.count == 40);
    free_list(&l);

    line[0] = '\0';
    for (int i = 0; i < MAX_LIST_ITEMS + 1; i++)
        strcat(line, i ? ";x" : "x");
    CHECK(lex_and_parse(line, &l) == -1);
}

int main(void)
{
    test_simple();
    test_pipeline_and_redirects();
    test_lists();
    test_syntax_errors();
    test_limits();
    printf("test_parser: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
