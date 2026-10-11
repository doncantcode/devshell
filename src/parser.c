#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "parser.h"

void free_pipeline(Pipeline *p)
{
    for (int i = 0; i < MAX_CMDS; i++) {
        Command *c = &p->cmds[i];
        for (int j = 0; j < MAX_ARGS; j++)
            free(c->argv[j]);
        free(c->infile);
        free(c->outfile);
    }
    memset(p, 0, sizeof *p);
}

void free_list(CommandList *l)
{
    for (int i = 0; i < l->count; i++)
        free_pipeline(&l->items[i].pipe);
    free(l->items);
    memset(l, 0, sizeof *l);
}

static const char *op_text(TokenType t)
{
    switch (t) {
    case TOK_PIPE:   return "|";
    case TOK_IN:     return "<";
    case TOK_OUT:    return ">";
    case TOK_APPEND: return ">>";
    case TOK_BG:     return "&";
    case TOK_SEMI:   return ";";
    case TOK_AND:    return "&&";
    case TOK_OR:     return "||";
    default:         return "?";
    }
}

/* Append a zeroed pipeline (with its first, empty stage) to the list. */
static int begin_item(CommandList *l)
{
    if (l->count >= MAX_LIST_ITEMS) {
        err_syntax("too many commands on one line (max %d)", MAX_LIST_ITEMS);
        return -1;
    }
    if (l->count == l->cap) {
        int ncap = l->cap ? l->cap * 2 : 4;
        ListItem *p = realloc(l->items, (size_t)ncap * sizeof *p);
        if (!p) {
            err_msg("out of memory");
            return -1;
        }
        l->items = p;
        l->cap = ncap;
    }
    memset(&l->items[l->count], 0, sizeof l->items[0]);
    l->items[l->count].pipe.count = 1;
    l->count++;
    return 0;
}

/* list     → and_or ( (";" | "&") and_or )* [ ";" | "&" ]
 * and_or   → pipeline ( ("&&" | "||") pipeline )*
 * pipeline → command ( "|" command )*
 * command  → WORD ( WORD | redirect )*
 * redirect → ( "<" | ">" | ">>" ) WORD
 *
 * A trailing "&" runs that one pipeline in the background. It is rejected
 * after "&&" / "||" because backgrounding a whole and-or chain would need a
 * sub-shell, which DevShell does not have. */
int parse(const Token *toks, int n, CommandList *l)
{
    Pipeline *pl;
    Command *cur;
    int argc = 0, chain = 0;
    TokenType prev = TOK_SEMI;

    memset(l, 0, sizeof *l);
    if (n == 0)
        return 1;
    if (begin_item(l) < 0)
        goto fail;
    pl = &l->items[0].pipe;
    cur = &pl->cmds[0];

    for (int i = 0; i < n; i++) {
        switch (toks[i].type) {
        case TOK_WORD:
            if (argc >= MAX_ARGS - 1) {
                err_syntax("too many arguments (max %d)", MAX_ARGS - 2);
                goto fail;
            }
            cur->argv[argc++] = xstrdup(toks[i].text);
            break;

        case TOK_PIPE:
            if (argc == 0) {
                err_syntax("missing command before '|'");
                goto fail;
            }
            if (pl->count >= MAX_CMDS) {
                err_syntax("too many pipeline stages (max %d)", MAX_CMDS);
                goto fail;
            }
            cur = &pl->cmds[pl->count++];
            argc = 0;
            break;

        case TOK_IN:
        case TOK_OUT:
        case TOK_APPEND:
            if (i + 1 >= n || toks[i + 1].type != TOK_WORD) {
                err_syntax("expected a file name after '%s'", op_text(toks[i].type));
                goto fail;
            }
            if (toks[i].type == TOK_IN) {
                free(cur->infile);
                cur->infile = xstrdup(toks[i + 1].text);
            } else {
                free(cur->outfile);
                cur->outfile = xstrdup(toks[i + 1].text);
                cur->append = (toks[i].type == TOK_APPEND);
            }
            i++;
            break;

        case TOK_SEMI:
        case TOK_AND:
        case TOK_OR:
        case TOK_BG: {
            Connector conn = toks[i].type == TOK_AND ? CONN_AND
                           : toks[i].type == TOK_OR  ? CONN_OR : CONN_SEQ;

            if (argc == 0) {
                err_syntax("missing command before '%s'", op_text(toks[i].type));
                goto fail;
            }
            if (toks[i].type == TOK_BG) {
                if (chain > 0) {
                    err_syntax("'&' cannot follow a '&&' / '||' list");
                    goto fail;
                }
                cur->background = 1;
            }
            l->items[l->count - 1].next = conn;
            chain = (conn == CONN_SEQ) ? 0 : chain + 1;

            if (begin_item(l) < 0)
                goto fail;
            pl = &l->items[l->count - 1].pipe;
            cur = &pl->cmds[0];
            argc = 0;
            break;
        }
        }
        prev = toks[i].type;
    }

    if (argc == 0) {
        if (prev == TOK_SEMI || prev == TOK_BG) {
            l->count--;                 /* harmless trailing ';' or '&' */
        } else {
            err_syntax("missing command after '%s'", op_text(prev));
            goto fail;
        }
    }
    return 0;

fail:
    free_list(l);
    return -1;
}

int lex_and_parse(const char *line, CommandList *l)
{
    Token toks[MAX_TOKENS];
    int n = 0;

    memset(l, 0, sizeof *l);
    if (lex(line, toks, MAX_TOKENS, &n) < 0)
        return -1;

    int rc = parse(toks, n, l);
    free_tokens(toks, n);
    return rc;
}
