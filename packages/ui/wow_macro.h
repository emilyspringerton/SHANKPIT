/* WOW-STYLE MACROS -- the bitfield compiler.
 *
 * WHAT THIS IS. "/cast [harm,nodead,@mouseover] Blaster; [@player] Mend" parsed ONCE into a fixed
 * array of clauses, each clause a pair of uint32 masks plus a unit token plus its argument. Evaluating
 * a clause is two ANDs, one comparison and a bounds check: no string comparison, no allocation, no
 * hashing at cast time. That is the whole performance argument for compiling instead of interpreting.
 *
 * WHAT IT IS NOT. There is no Lua here. A scripting VM is a dependency this engine does not vendor, and
 * inventing one would be a fake. What this gives a Lua binding is the layer underneath it: the
 * condition bits, the unit tokens, and wow_macro_execute() talking to a host vtable (see wow_api.h) --
 * so a binding's CastSpellByName("Blaster","mouseover") is one indirect call, and a text macro is the
 * same path. The textual surface a player types and the API surface an add-on calls converge here.
 *
 * SEMANTICS, chosen and stated:
 *   - clauses are tried IN ORDER and the FIRST whose conditions all hold runs; the rest are skipped
 *     (WoW's rule, and the reason "/cast [help] Mend; [harm] Zap" does not need an else).
 *   - several bracket groups accumulate: "[harm][nodead]" == "[harm,nodead]".
 *   - "no<word>" sets a FORBID bit instead of a REQUIRE bit, so [nodead] is "not dead" rather than a
 *     second condition word to special-case.
 *   - with no [@token] a clause targets the engine's CURRENT target; [@none] is a real token meaning
 *     "no unit", which is how [noexists] is tested.
 *   - a clause that does not compile is REJECTED with a specific negative code. No macro is silently
 *     reinterpreted as something the player did not write.
 */
#ifndef SHANKPIT_UI_WOW_MACRO_H
#define SHANKPIT_UI_WOW_MACRO_H

#include <stdint.h>
#include <string.h>

#define WOW_MACRO_TEXT_MAX   192
#define WOW_MACRO_ARG_MAX     48
#define WOW_MACRO_CLAUSE_MAX   4

/* The condition bits. These ARE the compiled representation: a clause holds two masks of these. */
enum {
    WOW_COND_EXISTS  = 1u << 0,
    WOW_COND_HARM    = 1u << 1,
    WOW_COND_HELP    = 1u << 2,
    WOW_COND_DEAD    = 1u << 3,
    WOW_COND_STEALTH = 1u << 4,
    WOW_COND_COMBAT  = 1u << 5
};

/* Unit tokens. 0 is "the clause said nothing", so it can inherit the engine default (target). */
enum {
    WOW_TOKEN_NONE = 0,          /* unset: follow the current target */
    WOW_TOKEN_PLAYER,
    WOW_TOKEN_TARGET,
    WOW_TOKEN_FOCUS,
    WOW_TOKEN_MOUSEOVER,
    WOW_TOKEN_ARENA1,
    WOW_TOKEN_ARENA2,
    WOW_TOKEN_PARTY1,
    WOW_TOKEN_NOBODY = 255       /* [@none]: a unit that provably does not exist */
};

enum {
    WOW_CMD_CAST = 1,
    WOW_CMD_USE,
    WOW_CMD_TARGET,
    WOW_CMD_CLEARTARGET,
    WOW_CMD_FOCUS,
    WOW_CMD_CLEARFOCUS,
    WOW_CMD_STARTATTACK
};

typedef struct {
    uint32_t      require_bits;   /* must all be set */
    uint32_t      forbid_bits;    /* must all be clear */
    unsigned char token;          /* WOW_TOKEN_*, 0 = engine default */
    char          arg[WOW_MACRO_ARG_MAX];
} WowClause;

typedef struct {
    unsigned char cmd;            /* WOW_CMD_* */
    int           clause_count;
    WowClause     clause[WOW_MACRO_CLAUSE_MAX];
} WowMacroLine;

/* What the engine tells the evaluator about one unit. Deliberately a snapshot, not a pointer into a
   simulation struct: the macro layer must not care which actor pool is behind the token. */
typedef struct {
    unsigned char exists;
    unsigned char dead;
    unsigned char hostile;
    unsigned char stealth;
    unsigned char combat;   /* the ASKER's combat state for [combat], or the unit's when it is the player */
} WowUnit;

typedef WowUnit (*WowUnitFn)(void *ctx, int token);

static inline char *wow_trim(char *s) {
    char *e;
    if (!s) return s;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
    return s;
}

static inline int wow_word_is(const char *w, size_t len, const char *lit) {
    return strlen(lit) == len && memcmp(w, lit, len) == 0;
}

static inline int wow_cmd_from(const char *w, size_t len) {
    if (wow_word_is(w, len, "cast")) return WOW_CMD_CAST;
    if (wow_word_is(w, len, "use")) return WOW_CMD_USE;
    if (wow_word_is(w, len, "target")) return WOW_CMD_TARGET;
    if (wow_word_is(w, len, "cleartarget")) return WOW_CMD_CLEARTARGET;
    if (wow_word_is(w, len, "focus")) return WOW_CMD_FOCUS;
    if (wow_word_is(w, len, "clearfocus")) return WOW_CMD_CLEARFOCUS;
    if (wow_word_is(w, len, "startattack")) return WOW_CMD_STARTATTACK;
    return 0;
}

static inline const char *wow_cmd_name(int cmd) {
    switch (cmd) {
        case WOW_CMD_CAST: return "cast";
        case WOW_CMD_USE: return "use";
        case WOW_CMD_TARGET: return "target";
        case WOW_CMD_CLEARTARGET: return "cleartarget";
        case WOW_CMD_FOCUS: return "focus";
        case WOW_CMD_CLEARFOCUS: return "clearfocus";
        case WOW_CMD_STARTATTACK: return "startattack";
        default: return "?";
    }
}

/* Returns the token, or -1 for a word this compiler does not know. */
static inline int wow_token_from(const char *w, size_t len) {
    if (wow_word_is(w, len, "player")) return WOW_TOKEN_PLAYER;
    if (wow_word_is(w, len, "target")) return WOW_TOKEN_TARGET;
    if (wow_word_is(w, len, "focus")) return WOW_TOKEN_FOCUS;
    if (wow_word_is(w, len, "mouseover")) return WOW_TOKEN_MOUSEOVER;
    if (wow_word_is(w, len, "arena1")) return WOW_TOKEN_ARENA1;
    if (wow_word_is(w, len, "arena2")) return WOW_TOKEN_ARENA2;
    if (wow_word_is(w, len, "party1")) return WOW_TOKEN_PARTY1;
    if (wow_word_is(w, len, "none")) return WOW_TOKEN_NOBODY;
    return -1;
}

/* Positive words set a REQUIRE bit; "no"+word sets the FORBID bit. Returns 0, or -1 if unknown. */
static inline int wow_cond_from(const char *w, size_t len, uint32_t *req, uint32_t *forb) {
    static const struct { const char *n; uint32_t bit; } tab[] = {
        { "exists",  WOW_COND_EXISTS  },
        { "harm",    WOW_COND_HARM    },
        { "help",    WOW_COND_HELP    },
        { "dead",    WOW_COND_DEAD    },
        { "stealth", WOW_COND_STEALTH },
        { "combat",  WOW_COND_COMBAT  }
    };
    size_t i;
    int negated = (len > 2 && w[0] == 'n' && w[1] == 'o');
    const char *word = negated ? w + 2 : w;
    size_t wl = negated ? len - 2 : len;
    for (i = 0; i < sizeof tab / sizeof tab[0]; i++) {
        if (!wow_word_is(word, wl, tab[i].n)) continue;
        if (negated) *forb |= tab[i].bit;
        else         *req  |= tab[i].bit;
        return 0;
    }
    return -1;
}

/* 0 for a target/focus clause with a token to target, 1 when an argument is mandatory. */
static inline int wow_clause_needs_arg(int cmd, const WowClause *c) {
    if (cmd == WOW_CMD_CLEARTARGET || cmd == WOW_CMD_CLEARFOCUS || cmd == WOW_CMD_STARTATTACK) return 0;
    if (cmd == WOW_CMD_TARGET || cmd == WOW_CMD_FOCUS) return !(c && c->token);
    return 1;   /* cast, use: an empty spell name is not a spell */
}

/* Compile one line of macro text.
     1  compiled
     0  nothing to compile (blank)
    -1  unknown command
    -2  malformed: unclosed bracket, unknown condition, unknown token
    -3  more than WOW_MACRO_CLAUSE_MAX clauses
    -4  an argument longer than WOW_MACRO_ARG_MAX
    -5  an argument was required and missing
    -6  the line itself is longer than WOW_MACRO_TEXT_MAX
   A leading '/' is optional, so chat text and a bound key compile identically. */
static inline int wow_macro_parse(const char *text, WowMacroLine *out) {
    char buf[WOW_MACRO_TEXT_MAX];
    char *p;
    size_t n;
    int cmd;
    if (!out) return -2;
    memset(out, 0, sizeof *out);
    if (!text) return 0;
    n = strlen(text);
    if (n >= sizeof buf) return -6;
    memcpy(buf, text, n + 1);
    p = wow_trim(buf);
    if (*p == '/') p = wow_trim(p + 1);
    if (!*p) return 0;

    {
        char *w = p;
        while (*p && *p != ' ' && *p != '\t' && *p != ';') p++;
        cmd = wow_cmd_from(w, (size_t)(p - w));
        if (cmd <= 0) return -1;
        out->cmd = (unsigned char)cmd;
        if (*p) *p++ = '\0';
    }

    for (;;) {
        WowClause *c;
        while (*p == ' ' || *p == '\t' || *p == ';') p++;
        if (!*p) break;
        if (out->clause_count >= WOW_MACRO_CLAUSE_MAX) return -3;
        c = &out->clause[out->clause_count];

        for (;;) {
            char *close, *q;
            while (*p == ' ' || *p == '\t') p++;
            if (*p != '[') break;
            p++;
            close = strchr(p, ']');
            if (!close) return -2;
            *close = '\0';
            q = p;
            for (;;) {
                char *term;
                while (*q == ',' || *q == ' ' || *q == '\t') q++;
                if (!*q) break;
                term = q;
                while (*q && *q != ',' && *q != ' ' && *q != '\t') q++;
                if (*q) *q++ = '\0';
                if (*term == '@') {
                    int t = wow_token_from(term + 1, strlen(term + 1));
                    if (t < 0) return -2;
                    c->token = (unsigned char)t;
                } else {
                    uint32_t req = 0, forb = 0;
                    if (wow_cond_from(term, strlen(term), &req, &forb) < 0) return -2;
                    c->require_bits |= req;
                    c->forbid_bits |= forb;
                }
            }
            p = close + 1;
        }

        {
            char *arg = p, *semi = strchr(p, ';');
            size_t alen;
            if (semi) *semi = '\0';
            arg = wow_trim(arg);
            alen = strlen(arg);
            if (alen >= WOW_MACRO_ARG_MAX) return -4;
            memcpy(c->arg, arg, alen + 1);
            if (!alen && wow_clause_needs_arg(cmd, c)) return -5;
            out->clause_count++;
            if (!semi) break;
            p = semi + 1;
        }
    }

    if (out->clause_count == 0) {
        if (wow_clause_needs_arg(cmd, NULL)) return -5;
        out->clause_count = 1;   /* an argument-free command still needs one clause to execute */
    }
    return 1;
}

/* Does this clause's condition hold for this unit? Two mask tests, no strings. */
static inline int wow_clause_matches(const WowClause *c, void *ctx, WowUnitFn fn) {
    WowUnit u;
    uint32_t bits = 0;
    int token;
    if (!c || !fn) return 0;
    token = c->token ? (int)c->token : WOW_TOKEN_TARGET;
    memset(&u, 0, sizeof u);
    if (token != WOW_TOKEN_NOBODY) u = fn(ctx, token);
    if (u.exists) {
        bits |= WOW_COND_EXISTS;
        bits |= u.hostile ? WOW_COND_HARM : WOW_COND_HELP;   /* harm/help only mean something if it exists */
    }
    if (u.dead)    bits |= WOW_COND_DEAD;
    if (u.stealth) bits |= WOW_COND_STEALTH;
    if (u.combat)  bits |= WOW_COND_COMBAT;
    if ((bits & c->require_bits) != c->require_bits) return 0;
    if (bits & c->forbid_bits) return 0;
    return 1;
}

/* First matching clause wins. Returns 1 and fills token/arg, or 0 when no clause holds. */
static inline int wow_macro_resolve(const WowMacroLine *m, void *ctx, WowUnitFn fn,
                                    int *token_out, const char **arg_out) {
    int i;
    if (!m || !fn) return 0;
    for (i = 0; i < m->clause_count; i++) {
        if (!wow_clause_matches(&m->clause[i], ctx, fn)) continue;
        if (token_out) *token_out = m->clause[i].token ? (int)m->clause[i].token : WOW_TOKEN_TARGET;
        if (arg_out) *arg_out = m->clause[i].arg;
        return 1;
    }
    return 0;
}

#endif /* SHANKPIT_UI_WOW_MACRO_H */
