#ifndef LEXER_H
#define LEXER_H

typedef enum {
    TOK_WORD,
    TOK_PIPE,     /* |  */
    TOK_IN,       /* <  */
    TOK_OUT,      /* >  */
    TOK_APPEND,   /* >> */
    TOK_BG,       /* &  */
    TOK_SEMI,     /* ;  */
    TOK_AND,      /* && */
    TOK_OR        /* || */
} TokenType;

typedef struct {
    TokenType type;
    char     *text;   /* TOK_WORD: the RAW source text, quotes and $ intact
                         (quote removal and expansion happen later, in
                         expand.c, so they see up-to-date variables).
                         NULL for operators. */
} Token;

/* Split `line` into at most `max` tokens. Quotes ('...' and "...") make
 * whitespace and operators part of a word; backslash escapes the next
 * character. Returns 0 on success; on a lexical error prints a message,
 * frees any tokens already made, sets *count to 0 and returns -1. */
int lex(const char *line, Token *toks, int max, int *count);

void free_tokens(Token *toks, int count);

/* Printable name of a token type ("PIPE", "AND", ...). */
const char *token_name(TokenType t);

#endif
