#ifndef EDU_LEXER_H
#define EDU_LEXER_H

typedef enum {
    EDU_TOK_EOF = 0,
    EDU_TOK_IDENT,
    EDU_TOK_NUMBER,
    EDU_TOK_STRING,
    EDU_TOK_LPAREN,
    EDU_TOK_RPAREN,
    EDU_TOK_LBRACE,
    EDU_TOK_RBRACE,
    EDU_TOK_LBRACKET,
    EDU_TOK_RBRACKET,
    EDU_TOK_SEMI,
    EDU_TOK_COMMA,
    EDU_TOK_PLUS,
    EDU_TOK_MINUS,
    EDU_TOK_STAR,
    EDU_TOK_SLASH,
    EDU_TOK_ASSIGN,
    EDU_TOK_EQ,
    EDU_TOK_LT,
    EDU_TOK_GT,
    EDU_TOK_IF,
    EDU_TOK_ELSE,
    EDU_TOK_LET,
    EDU_TOK_TRUE,
    EDU_TOK_FALSE,
    EDU_TOK_WHILE,
    EDU_TOK_RETURN,
    EDU_TOK_ARRAY
} EduTokenType;

typedef struct {
    EduTokenType type;
    const char *start;
    int length;
    int line;
    int col;
    int int_value;
} EduToken;

typedef struct {
    const char *src;
    int pos;
    int line;
    int col;
    EduToken current;
    char error[128];
} EduLexer;

void edu_lexer_init(EduLexer *lex, const char *src);
int edu_lexer_next(EduLexer *lex);

#endif
