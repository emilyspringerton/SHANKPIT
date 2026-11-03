/* MACROS -- WoW-style: a line of text bound to a key that runs a list of commands.
 *
 * WHAT THIS FILE IS, AND IS NOT. It is the FORMAT: parse shankpit_macros.cfg, find a bind by key spec,
 * split a body on ';', write the file back. It is NOT a scripting language -- there is no Lua here and
 * no evaluator, because a capture path and a settings page do not need one. What each command DOES is
 * the client's job (apps/lobby/src/main.c, macro_cmd), so this stays header-only, allocation-free and
 * testable headless (packages/ui/macro_test.c).
 *
 * THE FILE (shankpit_macros.cfg), one bind per line, '#' comments:
 *
 *     bind ALT+1 stream.grade gritty; stream.detail light
 *     bind ALT+2 ambient +
 *     bind F6 say look at this
 *
 * SPEC is a key with optional ALT+/SHIFT+/CTRL+ prefixes; BODY is commands separated by ';'.
 * A macro is a MACRO, not a script: 192 bytes and 32 binds is the whole budget, and the same text
 * works typed into chat with a leading '/' -- so what a player tests in chat is exactly what a key
 * will do.
 */
#ifndef SHANKPIT_UI_MACRO_H
#define SHANKPIT_UI_MACRO_H

#include <stdio.h>
#include <string.h>

#define MACRO_MAX       32
#define MACRO_SPEC_MAX  24
#define MACRO_BODY_MAX  192
#define MACRO_WORD_MAX  32

typedef struct {
    char spec[MACRO_SPEC_MAX];   /* "F6", "ALT+1", "SHIFT+F7" -- the bind, not a display name */
    char body[MACRO_BODY_MAX];   /* one or more ';' separated commands */
} Macro;

typedef struct {
    Macro m[MACRO_MAX];
    int count;
} MacroBook;

static void macro_book_clear(MacroBook *b) {
    if (!b) return;
    b->count = 0;
}

/* ASCII-only case folding: the cross-build is mingw, where strcasecmp is _stricmp, so do it here. */
static char macro_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

static int macro_ci_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (macro_lower(*a) != macro_lower(*b)) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

/* Trim a line in place (leading and trailing space/tab/CR/LF) and return it. */
static char *macro_trim(char *s) {
    char *e;
    if (!s) return s;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
    return s;
}

static Macro *macro_find_spec(MacroBook *b, const char *spec) {
    int i;
    if (!b || !spec) return NULL;
    for (i = 0; i < b->count; i++)
        if (macro_ci_eq(b->m[i].spec, spec)) return &b->m[i];
    return NULL;
}

/* Bind SPEC to BODY, replacing an existing bind for the same key (that is the whole point: rebinding a
   key must not leave two macros racing for it). Returns 1 on success, 0 if SPEC or BODY is empty, -1 if
   the book is full. */
static int macro_set(MacroBook *b, const char *spec, const char *body) {
    Macro *slot = macro_find_spec(b, spec);
    if (!b || !spec || !*spec || !body || !*body) return 0;
    if (!slot) {
        if (b->count >= MACRO_MAX) return -1;
        slot = &b->m[b->count++];
        memset(slot, 0, sizeof *slot);
    }
    snprintf(slot->spec, sizeof slot->spec, "%s", spec);
    snprintf(slot->body, sizeof slot->body, "%s", body);
    return 1;
}

static int macro_unset(MacroBook *b, const char *spec) {
    int i;
    if (!b || !spec) return 0;
    for (i = 0; i < b->count; i++) {
        if (macro_ci_eq(b->m[i].spec, spec)) {
            b->m[i] = b->m[b->count - 1];
            b->count--;
            return 1;
        }
    }
    return 0;
}

/* One cfg line. Returns 1 if a bind was stored, 0 for blank/comment, -1 for a malformed line or a full
   book -- an unreadable line is reported, never silently reinterpreted as something else. */
static int macro_parse_line(MacroBook *b, const char *line) {
    char buf[MACRO_BODY_MAX + MACRO_SPEC_MAX + 8];
    char *p, *spec, *body;
    int n;
    if (!b || !line) return -1;
    n = snprintf(buf, sizeof buf, "%s", line);
    if (n < 0) return -1;
    if ((size_t)n >= sizeof buf) return -1;   /* a line that long is not a macro */
    p = macro_trim(buf);
    if (!*p || *p == '#') return 0;
    if (strncmp(p, "bind", 4) != 0 && strncmp(p, "BIND", 4) != 0 && strncmp(p, "Bind", 4) != 0) return -1;
    if (p[4] != ' ' && p[4] != '\t') return -1;
    p = macro_trim(p + 4);
    spec = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    if (!*p) return -1;                       /* a spec with no command does nothing: reject it */
    *p++ = '\0';
    body = macro_trim(p);
    if (!*body) return -1;
    return macro_set(b, spec, body) > 0 ? 1 : -1;
}

/* Read a whole file's text. Returns binds stored; *bad counts rejected lines (comments excluded). */
static int macro_load_text(MacroBook *b, const char *text, int *bad) {
    int stored = 0, rejected = 0;
    const char *p = text;
    if (bad) *bad = 0;
    if (!b || !text) return 0;
    while (*p) {
        char line[MACRO_BODY_MAX + MACRO_SPEC_MAX + 8];
        size_t len = 0;
        while (*p && *p != '\n') {
            if (len + 1 < sizeof line) line[len++] = *p;
            p++;
        }
        line[len] = '\0';
        if (*p == '\n') p++;
        {
            int r = macro_parse_line(b, line);
            if (r > 0) stored++;
            else if (r < 0) rejected++;
        }
    }
    if (bad) *bad = rejected;
    return stored;
}

/* Serialise the whole book. Returns the number of bytes written, or -1 if it does not fit in cap. */
static int macro_save_text(const MacroBook *b, char *out, int cap) {
    int i, used = 0;
    if (!b || !out || cap <= 0) return -1;
    out[0] = '\0';
    {
        const char *header =
            "# SHANKPIT macros. One bind per line; '#' starts a comment.\n"
            "#   bind SPEC command; command; ...\n"
            "# SPEC is a key with optional ALT+/SHIFT+/CTRL+, e.g. F6, ALT+1, SHIFT+F7.\n"
            "# The same text works typed into chat with a leading '/' -- test it there first.\n"
            "# Commands: stream.grade, stream.detail, stream, ambient, bullet.holes, brick.debris,\n"
            "#           fullscreen, broadcast, say, bind, macro, help\n"
            "# WoW-style: cast <weapon> [@token] [harm|help|dead|nodead|combat|noexists], use <1-9>,\n"
            "#           target, focus, cleartarget, clearfocus, startattack -- e.g.\n"
            "#           bind F6 cast [harm,nodead,@mouseover] magnum; [@player] knife\n";
        int n = snprintf(out + used, (size_t)(cap - used), "%s", header);
        if (n < 0 || n >= cap - used) return -1;
        used += n;
    }
    for (i = 0; i < b->count; i++) {
        int n = snprintf(out + used, (size_t)(cap - used), "bind %s %s\n", b->m[i].spec, b->m[i].body);
        if (n < 0 || n >= cap - used) return -1;
        used += n;
    }
    return used;
}

/* Split a body into commands: *cursor walks it, each call fills out with the next non-empty command and
   returns 1, or returns 0 at the end. Handles "a;; b ;" without producing empty commands. */
static int macro_next(const char *body, int *cursor, char *out, int cap) {
    int i, n = 0;
    if (!body || !cursor || !out || cap <= 0) return 0;
    i = *cursor;
    for (;;) {
        while (body[i] == ' ' || body[i] == '\t' || body[i] == ';') i++;
        if (!body[i]) { *cursor = i; out[0] = '\0'; return 0; }
        break;
    }
    while (body[i] && body[i] != ';') {
        if (n + 1 < cap) out[n++] = body[i];
        i++;
    }
    out[n] = '\0';
    macro_trim(out);
    *cursor = i;
    return out[0] != '\0';
}

#endif /* SHANKPIT_UI_MACRO_H */
