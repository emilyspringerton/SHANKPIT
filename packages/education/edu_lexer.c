#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "edu_lexer.h"

static char peek(EduLexer *lex) { return lex->src[lex->pos]; }
static char adv(EduLexer *lex) {
    char c = lex->src[lex->pos++];
    if (c == '\n') { lex->line++; lex->col = 1; }
    else lex->col++;
    return c;
}

void edu_lexer_init(EduLexer *lex, const char *src) {
    memset(lex, 0, sizeof(*lex));
    lex->src = src;
    lex->line = 1;
    lex->col = 1;
    edu_lexer_next(lex);
}

static int is_ident_start(char c) { return isalpha((unsigned char)c) || c == '_'; }
static int is_ident(char c) { return isalnum((unsigned char)c) || c == '_'; }

static EduTokenType kw(const char *s, int n) {
    if (n == 2 && strncmp(s, "if", 2) == 0) return EDU_TOK_IF;
    if (n == 4 && strncmp(s, "else", 4) == 0) return EDU_TOK_ELSE;
    if (n == 3 && strncmp(s, "let", 3) == 0) return EDU_TOK_LET;
    if (n == 4 && strncmp(s, "true", 4) == 0) return EDU_TOK_TRUE;
    if (n == 5 && strncmp(s, "false", 5) == 0) return EDU_TOK_FALSE;
    if (n == 5 && strncmp(s, "while", 5) == 0) return EDU_TOK_WHILE;
    if (n == 6 && strncmp(s, "return", 6) == 0) return EDU_TOK_RETURN;
    if (n == 5 && strncmp(s, "array", 5) == 0) return EDU_TOK_ARRAY;
    return EDU_TOK_IDENT;
}

int edu_lexer_next(EduLexer *lex) {
    while (peek(lex) && isspace((unsigned char)peek(lex))) adv(lex);
    EduToken t;
    memset(&t, 0, sizeof(t));
    t.start = lex->src + lex->pos;
    t.line = lex->line;
    t.col = lex->col;

    char c = peek(lex);
    if (!c) { t.type = EDU_TOK_EOF; lex->current = t; return 1; }

    if (is_ident_start(c)) {
        int st = lex->pos;
        while (is_ident(peek(lex))) adv(lex);
        t.start = lex->src + st;
        t.length = lex->pos - st;
        t.type = kw(t.start, t.length);
        lex->current = t;
        return 1;
    }
    if (isdigit((unsigned char)c)) {
        int val = 0;
        int st = lex->pos;
        while (isdigit((unsigned char)peek(lex))) val = val * 10 + (adv(lex) - '0');
        t.start = lex->src + st;
        t.length = lex->pos - st;
        t.type = EDU_TOK_NUMBER;
        t.int_value = val;
        lex->current = t;
        return 1;
    }

    adv(lex);
    t.length = 1;
    switch (c) {
        case '(': t.type = EDU_TOK_LPAREN; break;
        case ')': t.type = EDU_TOK_RPAREN; break;
        case '{': t.type = EDU_TOK_LBRACE; break;
        case '}': t.type = EDU_TOK_RBRACE; break;
        case '[': t.type = EDU_TOK_LBRACKET; break;
        case ']': t.type = EDU_TOK_RBRACKET; break;
        case ';': t.type = EDU_TOK_SEMI; break;
        case ',': t.type = EDU_TOK_COMMA; break;
        case '+': t.type = EDU_TOK_PLUS; break;
        case '-': t.type = EDU_TOK_MINUS; break;
        case '*': t.type = EDU_TOK_STAR; break;
        case '/': t.type = EDU_TOK_SLASH; break;
        case '<': t.type = EDU_TOK_LT; break;
        case '>': t.type = EDU_TOK_GT; break;
        case '=':
            if (peek(lex) == '=') { adv(lex); t.type = EDU_TOK_EQ; t.length = 2; }
            else t.type = EDU_TOK_ASSIGN;
            break;
        case '"': {
            int st = lex->pos;
            while (peek(lex) && peek(lex) != '"' && peek(lex) != '\n') adv(lex);
            if (peek(lex) != '"') {
                snprintf(lex->error, sizeof(lex->error), "unterminated string at %d:%d", t.line, t.col);
                return 0;
            }
            t.type = EDU_TOK_STRING;
            t.start = lex->src + st;
            t.length = lex->pos - st;
            adv(lex);
            lex->current = t;
            return 1;
        }
        default:
            snprintf(lex->error, sizeof(lex->error), "unknown token '%c' at %d:%d", c, t.line, t.col);
            return 0;
    }
    lex->current = t;
    return 1;
}
