/* Headless checks for packages/ui/macro.h: the FORMAT only -- parse, rebind, split, roundtrip, bounds.
   What a command does is the client's business and is not tested here. */
#include <stdio.h>
#include <string.h>

#include "macro.h"

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void) {
    MacroBook b;
    macro_book_clear(&b);

    CHECK(macro_parse_line(&b, "# a comment") == 0, "a comment was not skipped");
    CHECK(macro_parse_line(&b, "   \t ") == 0, "a blank line was not skipped");
    CHECK(macro_parse_line(&b, "bind F6 stream.grade +") == 1, "a bind was not stored");
    CHECK(b.count == 1, "count %d, want 1", b.count);
    CHECK(strcmp(b.m[0].spec, "F6") == 0, "spec '%s'", b.m[0].spec);
    CHECK(strcmp(b.m[0].body, "stream.grade +") == 0, "body '%s'", b.m[0].body);
    CHECK(macro_parse_line(&b, "bogus F6 stream.grade +") == -1, "a malformed line was accepted");
    CHECK(macro_parse_line(&b, "bind F6") == -1, "a spec with no command was accepted");
    CHECK(macro_parse_line(&b, "bind F7") == -1, "a bind with no body was accepted");
    CHECK(macro_parse_line(&b, "bind   F8    ambient +   ") == 1, "extra spaces were not tolerated");
    CHECK(strcmp(macro_find_spec(&b, "f8")->body, "ambient +") == 0, "spec lookup is not case-insensitive");

    /* rebinding a key REPLACES: two macros must never race for the same key */
    CHECK(macro_parse_line(&b, "bind f6 ambient -") == 1, "a case-different rebind failed");
    CHECK(b.count == 2, "rebind added a bind instead of replacing: count %d", b.count);
    CHECK(strcmp(macro_find_spec(&b, "F6")->body, "ambient -") == 0, "rebind did not replace the body");
    CHECK(macro_unset(&b, "F6") == 1, "unset failed");
    CHECK(macro_find_spec(&b, "F6") == NULL, "unset left the bind behind");

    /* ';' split, tolerating an empty piece and loose spaces */
    {   int cur = 0, n = 0;
        char out[MACRO_BODY_MAX];
        while (macro_next("stream.grade gritty; ;  ambient + ", &cur, out, sizeof out)) {
            n++;
            if (n == 1) CHECK(strcmp(out, "stream.grade gritty") == 0, "first '%s'", out);
            if (n == 2) CHECK(strcmp(out, "ambient +") == 0, "second '%s'", out);
        }
        CHECK(n == 2, "split produced %d commands, want 2", n);
    }
    {   int cur = 0;
        char out[MACRO_BODY_MAX];
        CHECK(macro_next(";;", &cur, out, sizeof out) == 0, "an empty body produced a command");
    }

    /* save -> load roundtrip keeps specs and bodies */
    {   static char text[8192];
        MacroBook c;
        int bad = 0;
        macro_parse_line(&b, "bind ALT+1 stream.grade gritty; stream.detail light");
        CHECK(macro_save_text(&b, text, sizeof text) > 0, "save failed");
        macro_book_clear(&c);
        CHECK(macro_load_text(&c, text, &bad) == 2, "reload stored %d binds, want 2", c.count);
        CHECK(bad == 0, "%d lines rejected on reload", bad);
        CHECK(strcmp(c.m[0].spec, "F8") == 0 && strcmp(c.m[0].body, "ambient +") == 0, "roundtrip changed bind 0");
        CHECK(strcmp(c.m[1].spec, "ALT+1") == 0, "roundtrip changed bind 1 spec '%s'", c.m[1].spec);
        CHECK(strcmp(c.m[1].body, "stream.grade gritty; stream.detail light") == 0, "roundtrip changed a body");
        CHECK(macro_save_text(&b, text, 16) == -1, "a buffer too small to hold the book was not refused");
    }

    /* the book is bounded: 32 binds, then a refusal -- never a silent overwrite */
    {   MacroBook d;
        int i, refused = 0;
        macro_book_clear(&d);
        for (i = 0; i < MACRO_MAX; i++) {
            char spec[16];
            snprintf(spec, sizeof spec, "M%d", i);
            CHECK(macro_set(&d, spec, "ambient +") == 1, "bind %d refused", i);
        }
        refused = macro_set(&d, "M99", "ambient +");
        CHECK(refused == -1, "a 33rd bind returned %d, want -1", refused);
        CHECK(d.count == MACRO_MAX, "count %d after overfill", d.count);
    }

    /* a body longer than the budget truncates to the budget, and is still a valid bind */
    {   MacroBook e;
        char longbody[600];
        int i;
        macro_book_clear(&e);
        for (i = 0; i < (int)sizeof longbody - 1; i++) longbody[i] = 'a';
        longbody[sizeof longbody - 1] = '\0';
        CHECK(macro_set(&e, "F6", longbody) == 1, "an overlong body was refused instead of truncated");
        CHECK(strlen(e.m[0].body) == MACRO_BODY_MAX - 1, "body length %d, want %d", (int)strlen(e.m[0].body), MACRO_BODY_MAX - 1);
    }

    if (fails) { printf("macro: %d FAILED\n", fails); return 1; }
    printf("macro: all checks passed\n");
    return 0;
}
