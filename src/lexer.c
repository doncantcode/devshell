#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "lexer.h"

static int is_op_char(char c)
{
    return c == '|' || c == '<' || c == '>' || c == '&' || c == ';';
}

const char *token_name(TokenType t)
{
    switch (t) {
    case TOK_WORD:   return "WORD";
    case TOK_PIPE:   return "PIPE";
    case TOK_IN:     return "IN";
    case TOK_OUT:    return "OUT";
    case TOK_APPEND: return "APPEND";
    case TOK_BG:     return "BG";
    case TOK_SEMI:   return "SEMI";
    case TOK_AND:    return "AND";
    case TOK_OR:     return "OR";
    }
    return "?";
}

static int add_token(Token *toks, int max, int *n, TokenType type, char *text)
{
    if (*n >= max) {
        free(text);
        err_syntax("too many tokens (max %d)", max);
        return -1;
    }
    toks[*n].type = type;
    toks[*n].text = text;
    (*n)++;
    return 0;
}

static char *dup_range(const char *s, size_t len)
{
    char *p = malloc(len + 1);
    if (!p) {
        err_msg("out of memory");
        exit(1);
    }
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}

int lex(const char *line, Token *toks, int max, int *count)
{
    int n = 0;
    size_t i = 0;

    while (line[i]) {
        char c = line[i];

        if (isspace((unsigned char)c)) {
            i++;
            continue;
        }

        if (is_op_char(c)) {
            TokenType t;
            if (c == '|') {
                if (line[i + 1] == '|') { t = TOK_OR; i++; } else t = TOK_PIPE;
            } else if (c == '&') {
                if (line[i + 1] == '&') { t = TOK_AND; i++; } else t = TOK_BG;
            } else if (c == ';') {
                t = TOK_SEMI;
            } else if (c == '<') {
                t = TOK_IN;
            } else {
                if (line[i + 1] == '>') { t = TOK_APPEND; i++; } else t = TOK_OUT;
            }
            i++;
            if (add_token(toks, max, &n, t, NULL) < 0)
                goto fail;
            continue;
        }

        /* A word: runs of plain characters and quoted sections glue together,
         * so  a"b c"d  is one word. The token keeps the raw text; quotes are
         * removed later by expand_word(). */
        size_t start = i;
        while (line[i] && !isspace((unsigned char)line[i]) && !is_op_char(line[i])) {
            if (line[i] == '\'') {
                i++;
                while (line[i] && line[i] != '\'')
                    i++;
                if (!line[i]) {
                    err_syntax("unmatched single quote");
                    goto fail;
                }
                i++;
            } else if (line[i] == '"') {
                i++;
                while (line[i] && line[i] != '"') {
                    if (line[i] == '\\' &&
                        (line[i + 1] == '"' || line[i + 1] == '\\' || line[i + 1] == '$'))
                        i++;
                    i++;
                }
                if (!line[i]) {
                    err_syntax("unmatched double quote");
                    goto fail;
                }
                i++;
            } else if (line[i] == '\\') {
                i += line[i + 1] ? 2 : 1;
            } else {
                i++;
            }
        }
        if (add_token(toks, max, &n, TOK_WORD, dup_range(line + start, i - start)) < 0)
            goto fail;
    }

    *count = n;
    return 0;

fail:
    free_tokens(toks, n);
    *count = 0;
    return -1;
}

void free_tokens(Token *toks, int count)
{
    for (int i = 0; i < count; i++) {
        free(toks[i].text);
        toks[i].text = NULL;
    }
}
