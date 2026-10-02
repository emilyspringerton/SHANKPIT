#include <string.h>
#include <stdio.h>
#include "edu_parser.h"
#include "edu_lexer.h"
#include "edu_bytecode.h"

typedef struct {
    EduLexer lex;
    EduCompileOutput *out;
    EduBytecodeWriter w;
} Parser;

static void set_error(Parser *p, const char *msg) {
    if (p->out->error[0]) return;
    snprintf(p->out->error, sizeof(p->out->error), "%s", msg);
    p->out->error_line = p->lex.current.line;
    p->out->error_col = p->lex.current.col;
}

static int next(Parser *p) {
    if (!edu_lexer_next(&p->lex)) {
        set_error(p, p->lex.error);
        return 0;
    }
    return 1;
}

static int expect(Parser *p, EduTokenType t, const char *msg) {
    if (p->lex.current.type != t) { set_error(p, msg); return 0; }
    return next(p);
}

// find_var returns the entry's index into p->out->vars[] -- for scalars this
// is numerically identical to .slot (add_var sets both to the same idx), but
// array entries never set .slot, so callers must use this index (not .slot)
// to work for both kinds. Callers that need vars[]-indexing for LOAD_VAR/
// STORE_VAR rely on that scalar/idx equivalence; callers that need
// .is_array/.arr_base/.arr_len index p->out->vars[] directly with this value.
static int find_var(Parser *p, const char *s, int n) {
    for (int i = 0; i < p->out->var_count; i++) {
        if ((int)strlen(p->out->vars[i].name) == n && strncmp(p->out->vars[i].name, s, n) == 0) return i;
    }
    return -1;
}

static int add_var(Parser *p, const char *s, int n) {
    if (p->out->var_count >= EDU_MAX_VARS) { set_error(p, "too many variables"); return -1; }
    int idx = p->out->var_count++;
    snprintf(p->out->vars[idx].name, sizeof(p->out->vars[idx].name), "%.*s", n, s);
    p->out->vars[idx].slot = idx;
    return idx;
}

// add_array_var registers 'name' as an array of length 'len', claiming 'len'
// slots from the shared arr_mem[] pool (see EDU_VM_ARR_MEM_MAX in
// edu_bytecode.h). Shares the same EduVarEntry table as scalars so a single
// find_var(name) lookup works for both -- callers check .is_array to tell
// them apart.
static int add_array_var(Parser *p, const char *s, int n, int len) {
    if (p->out->var_count >= EDU_MAX_VARS) { set_error(p, "too many variables"); return -1; }
    if (len <= 0) { set_error(p, "array length must be positive"); return -1; }
    if (p->out->arr_mem_used + len > EDU_VM_ARR_MEM_MAX) { set_error(p, "array memory exhausted"); return -1; }
    int idx = p->out->var_count++;
    snprintf(p->out->vars[idx].name, sizeof(p->out->vars[idx].name), "%.*s", n, s);
    p->out->vars[idx].is_array = 1;
    p->out->vars[idx].arr_base = p->out->arr_mem_used;
    p->out->vars[idx].arr_len = len;
    p->out->arr_mem_used += len;
    return idx;
}

static int add_string(Parser *p, const char *s, int n) {
    for (int i = 0; i < p->out->string_count; i++) {
        if ((int)strlen(p->out->string_storage[i]) == n && strncmp(p->out->string_storage[i], s, n) == 0) return i;
    }
    if (p->out->string_count >= EDU_MAX_STRINGS) { set_error(p, "too many strings"); return -1; }
    int idx = p->out->string_count++;
    snprintf(p->out->string_storage[idx], sizeof(p->out->string_storage[idx]), "%.*s", n, s);
    p->out->strings[idx] = p->out->string_storage[idx];
    return idx;
}

static int emit_u8(Parser *p, int v) { return edu_bc_emit_u8(&p->w, (unsigned char)v); }
static int emit_i32(Parser *p, int v) { return edu_bc_emit_i32(&p->w, v); }

static int parse_expr(Parser *p);

static int parse_primary(Parser *p) {
    EduToken tok = p->lex.current;
    if (tok.type == EDU_TOK_NUMBER) {
        emit_u8(p, EDU_OP_PUSH_INT); emit_i32(p, tok.int_value);
        return next(p);
    }
    if (tok.type == EDU_TOK_TRUE || tok.type == EDU_TOK_FALSE) {
        emit_u8(p, EDU_OP_PUSH_BOOL); emit_i32(p, tok.type == EDU_TOK_TRUE ? 1 : 0);
        return next(p);
    }
    if (tok.type == EDU_TOK_STRING) {
        int sidx = add_string(p, tok.start, tok.length);
        if (sidx < 0) return 0;
        emit_u8(p, EDU_OP_PUSH_STR); emit_i32(p, sidx);
        return next(p);
    }
    if (tok.type == EDU_TOK_IDENT) {
        const char *name = tok.start;
        int n = tok.length;
        if (!next(p)) return 0;
        if (p->lex.current.type == EDU_TOK_LPAREN) {
            int bid = -1, argc_expected = 0;
            if (!edu_binding_lookup(name, n, &bid, &argc_expected)) {
                set_error(p, "unknown function"); return 0;
            }
            if (!next(p)) return 0;
            int argc = 0;
            if (p->lex.current.type != EDU_TOK_RPAREN) {
                for (;;) {
                    if (!parse_expr(p)) return 0;
                    argc++;
                    if (p->lex.current.type == EDU_TOK_COMMA) { if (!next(p)) return 0; continue; }
                    break;
                }
            }
            if (argc != argc_expected) { set_error(p, "wrong arg count"); return 0; }
            if (!expect(p, EDU_TOK_RPAREN, "expected ')'")) return 0;
            emit_u8(p, EDU_OP_CALL_BUILTIN); emit_i32(p, bid); emit_i32(p, argc);
            return 1;
        }
        int vid = find_var(p, name, n);
        if (vid < 0) { set_error(p, "unknown variable"); return 0; }
        if (p->out->vars[vid].is_array) {
            if (p->lex.current.type != EDU_TOK_LBRACKET) { set_error(p, "expected '[' after array name"); return 0; }
            if (!next(p)) return 0; // consume '['
            if (!parse_expr(p)) return 0; // index expr -> pushes idx
            if (!expect(p, EDU_TOK_RBRACKET, "expected ']'")) return 0;
            emit_u8(p, EDU_OP_LOAD_ARR); emit_i32(p, p->out->vars[vid].arr_base); emit_i32(p, p->out->vars[vid].arr_len);
            return 1;
        }
        emit_u8(p, EDU_OP_LOAD_VAR); emit_i32(p, vid);
        return 1;
    }
    if (tok.type == EDU_TOK_LPAREN) {
        if (!next(p)) return 0;
        if (!parse_expr(p)) return 0;
        return expect(p, EDU_TOK_RPAREN, "expected ')' after expression");
    }
    set_error(p, "expected expression");
    return 0;
}

static int parse_unary(Parser *p) {
    if (p->lex.current.type == EDU_TOK_MINUS) {
        if (!next(p)) return 0;
        emit_u8(p, EDU_OP_PUSH_INT); emit_i32(p, 0);
        if (!parse_unary(p)) return 0;
        emit_u8(p, EDU_OP_SUB);
        return 1;
    }
    return parse_primary(p);
}

static int parse_factor(Parser *p) {
    if (!parse_unary(p)) return 0;
    while (p->lex.current.type == EDU_TOK_STAR || p->lex.current.type == EDU_TOK_SLASH) {
        EduTokenType op = p->lex.current.type;
        if (!next(p) || !parse_unary(p)) return 0;
        emit_u8(p, op == EDU_TOK_STAR ? EDU_OP_MUL : EDU_OP_DIV);
    }
    return 1;
}

static int parse_term(Parser *p) {
    if (!parse_factor(p)) return 0;
    while (p->lex.current.type == EDU_TOK_PLUS || p->lex.current.type == EDU_TOK_MINUS) {
        EduTokenType op = p->lex.current.type;
        if (!next(p) || !parse_factor(p)) return 0;
        emit_u8(p, op == EDU_TOK_PLUS ? EDU_OP_ADD : EDU_OP_SUB);
    }
    return 1;
}

static int parse_compare(Parser *p) {
    if (!parse_term(p)) return 0;
    while (p->lex.current.type == EDU_TOK_LT || p->lex.current.type == EDU_TOK_GT || p->lex.current.type == EDU_TOK_EQ) {
        EduTokenType op = p->lex.current.type;
        if (!next(p) || !parse_term(p)) return 0;
        emit_u8(p, op == EDU_TOK_LT ? EDU_OP_LT : op == EDU_TOK_GT ? EDU_OP_GT : EDU_OP_EQ);
    }
    return 1;
}

static int parse_expr(Parser *p) { return parse_compare(p); }

static int parse_stmt(Parser *p);

static int parse_block(Parser *p) {
    if (!expect(p, EDU_TOK_LBRACE, "expected '{'")) return 0;
    while (p->lex.current.type != EDU_TOK_RBRACE && p->lex.current.type != EDU_TOK_EOF) {
        if (!parse_stmt(p)) return 0;
    }
    return expect(p, EDU_TOK_RBRACE, "expected '}'");
}

static int parse_stmt(Parser *p) {
    if (p->lex.current.type == EDU_TOK_LET) {
        if (!next(p)) return 0;
        if (p->lex.current.type != EDU_TOK_IDENT) { set_error(p, "expected identifier"); return 0; }
        EduToken id = p->lex.current;
        if (!next(p) || !expect(p, EDU_TOK_ASSIGN, "expected '='")) return 0;
        // 'let name = array(N);' is a compile-time-only declaration -- it
        // claims N slots from the shared arr_mem[] pool and emits no
        // bytecode at all (arr_mem starts zero-initialized, same as vars[]).
        // Checked here, before falling through to the normal scalar path,
        // because 'array' parses identically to a function call otherwise
        // and would hit parse_primary's "unknown function" error.
        if (p->lex.current.type == EDU_TOK_ARRAY) {
            if (!next(p) || !expect(p, EDU_TOK_LPAREN, "expected '(' after 'array'")) return 0;
            if (p->lex.current.type != EDU_TOK_NUMBER) { set_error(p, "expected array length"); return 0; }
            int len = p->lex.current.int_value;
            if (!next(p) || !expect(p, EDU_TOK_RPAREN, "expected ')'")) return 0;
            if (add_array_var(p, id.start, id.length, len) < 0) return 0;
            return expect(p, EDU_TOK_SEMI, "expected ';'");
        }
        int var = add_var(p, id.start, id.length);
        if (var < 0) return 0;
        if (!parse_expr(p)) return 0;
        emit_u8(p, EDU_OP_STORE_VAR); emit_i32(p, var);
        return expect(p, EDU_TOK_SEMI, "expected ';'");
    }
    if (p->lex.current.type == EDU_TOK_IF) {
        if (!next(p)) return 0;
        if (p->lex.current.type == EDU_TOK_LPAREN) next(p);
        if (!parse_expr(p)) return 0;
        if (p->lex.current.type == EDU_TOK_RPAREN) next(p);
        int jfalse = edu_bc_emit_jump(&p->w, EDU_OP_JMP_IF_FALSE);
        if (!parse_block(p)) return 0;
        if (p->lex.current.type == EDU_TOK_ELSE) {
            if (!next(p)) return 0;
            int jend = edu_bc_emit_jump(&p->w, EDU_OP_JMP);
            edu_bc_patch_i32(&p->w, jfalse, p->w.len);
            if (!parse_block(p)) return 0;
            edu_bc_patch_i32(&p->w, jend, p->w.len);
        } else {
            edu_bc_patch_i32(&p->w, jfalse, p->w.len);
        }
        return 1;
    }
    if (p->lex.current.type == EDU_TOK_WHILE) {
        if (!next(p)) return 0;
        int loop_start = p->w.len;
        if (p->lex.current.type == EDU_TOK_LPAREN) next(p);
        if (!parse_expr(p)) return 0;
        if (p->lex.current.type == EDU_TOK_RPAREN) next(p);
        int jfalse = edu_bc_emit_jump(&p->w, EDU_OP_JMP_IF_FALSE);
        if (!parse_block(p)) return 0;
        emit_u8(p, EDU_OP_JMP); emit_i32(p, loop_start);
        edu_bc_patch_i32(&p->w, jfalse, p->w.len);
        return 1;
    }
    if (p->lex.current.type == EDU_TOK_RETURN) {
        if (!next(p)) return 0;
        if (p->lex.current.type != EDU_TOK_SEMI) {
            if (!parse_expr(p)) return 0;
        } else {
            emit_u8(p, EDU_OP_PUSH_INT); emit_i32(p, 0);
        }
        emit_u8(p, EDU_OP_RETURN);
        return expect(p, EDU_TOK_SEMI, "expected ';'");
    }
    if (p->lex.current.type == EDU_TOK_IDENT) {
        EduToken id = p->lex.current;
        if (!next(p)) return 0;
        if (p->lex.current.type == EDU_TOK_LBRACKET) {
            int vid = find_var(p, id.start, id.length);
            if (vid < 0 || !p->out->vars[vid].is_array) { set_error(p, "not an array"); return 0; }
            int base = p->out->vars[vid].arr_base, len = p->out->vars[vid].arr_len;
            if (!next(p)) return 0; // consume '['
            if (!parse_expr(p)) return 0; // index expr -> pushes idx
            if (!expect(p, EDU_TOK_RBRACKET, "expected ']'")) return 0;
            if (!expect(p, EDU_TOK_ASSIGN, "expected '=' after array index")) return 0;
            if (!parse_expr(p)) return 0; // value expr -> pushes val (on top of idx)
            emit_u8(p, EDU_OP_STORE_ARR); emit_i32(p, base); emit_i32(p, len);
            return expect(p, EDU_TOK_SEMI, "expected ';'");
        }
        if (p->lex.current.type == EDU_TOK_ASSIGN) {
            int vid = find_var(p, id.start, id.length);
            if (vid < 0) { set_error(p, "unknown variable in assignment"); return 0; }
            if (p->out->vars[vid].is_array) { set_error(p, "cannot assign directly to an array (use name[i] = ...)"); return 0; }
            if (!next(p) || !parse_expr(p)) return 0;
            emit_u8(p, EDU_OP_STORE_VAR); emit_i32(p, vid);
            return expect(p, EDU_TOK_SEMI, "expected ';'");
        }
        /* SHANKPIT fix (card #494): rewind so the identifier is `current` AGAIN with the lexer positioned just
         * AFTER it. The GFD original rewound pos to id.start, so the next token lexed was the identifier a
         * second time -> every bare builtin-call statement (`open_gate();`, `print(1);`) failed with "unknown
         * variable". Only `let`-led scripts ever compiled upstream. */
        p->lex.pos = (int)(id.start - p->lex.src) + id.length;
        p->lex.line = id.line;
        p->lex.col = id.col + id.length;
        p->lex.current = id;
    }

    if (!parse_expr(p)) return 0;
    emit_u8(p, EDU_OP_POP);
    return expect(p, EDU_TOK_SEMI, "expected ';'");
}

int edu_compile_source(const char *source, EduCompileOutput *out) {
    // Found live 2026-07-23 while testing new array support: the caller
    // convention (edu_script_compile_active, and every caller since) sets
    // out->bytecode/out->bytecode_cap BEFORE calling this function -- but
    // the memset(out, 0, ...) below wiped both back to NULL/0 first, so
    // edu_bc_init below always received a NULL buffer and zero capacity.
    // Every edu_bc_emit_u8/i32 call after that hits "len+1 > cap" (0+1 > 0)
    // immediately, so this function could never actually emit a single byte
    // of bytecode -- every real compile has been failing with "bytecode
    // overflow" on its very first statement. Preserve the caller's
    // buffer/cap across the reset instead of discarding them.
    unsigned char *caller_bytecode = out->bytecode;
    int caller_bytecode_cap = out->bytecode_cap;
    memset(out, 0, sizeof(*out));
    out->bytecode = caller_bytecode;
    out->bytecode_cap = caller_bytecode_cap;
    Parser p;
    memset(&p, 0, sizeof(p));
    p.out = out;
    edu_bc_init(&p.w, out->bytecode, out->bytecode_cap);
    edu_lexer_init(&p.lex, source);
    printf("[EDUSCRIPT] compile begin\n");

    while (p.lex.current.type != EDU_TOK_EOF && !out->error[0]) {
        if (!parse_stmt(&p)) break;
    }
    emit_u8(&p, EDU_OP_HALT);
    out->bytecode_len = p.w.len;
    out->ok = out->error[0] == 0 && !p.w.error;
    if (!out->ok) {
        if (p.w.error) snprintf(out->error, sizeof(out->error), "bytecode overflow");
        printf("[EDUSCRIPT] compile error line=%d col=%d msg=%s\n", out->error_line, out->error_col, out->error);
        return 0;
    }
    printf("[EDUSCRIPT] compile ok ops=%d\n", out->bytecode_len);
    return 1;
}
