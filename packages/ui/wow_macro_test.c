/* Headless tests for the WoW-compatible macro compiler, the host contract and the .toc loader.
 * No Lua, no window, no actors: the layers under a binding are exactly the layers that can be tested
 * without one, which is why they are separate from it. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "wow_macro.h"
#include "wow_api.h"
#include "addon_toc.h"
#include "wow_weapons.h"

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("wow: FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

typedef struct { int target_exists, target_dead, target_hostile; } World;

static WowUnit world_unit(void *p, int token) {
    const World *w = (const World *)p;
    WowUnit u;
    memset(&u, 0, sizeof u);
    if (token == WOW_TOKEN_TARGET) {
        u.exists = (unsigned char)w->target_exists;
        u.dead = (unsigned char)w->target_dead;
        u.hostile = (unsigned char)w->target_hostile;
    } else if (token == WOW_TOKEN_PLAYER || token == WOW_TOKEN_MOUSEOVER) {
        u.exists = 1;
        u.hostile = (unsigned char)(token == WOW_TOKEN_MOUSEOVER);
    }
    return u;
}

typedef struct {
    World w;
    int casts, token, targets, last_target, clears, focuses, last_focus, clears_focus, uses, last_slot;
    char spell[WOW_MACRO_ARG_MAX];
} Host;

static int h_exists(void *c, int t) {
    const Host *h = (const Host *)c;
    if (t == WOW_TOKEN_TARGET) return h->w.target_exists;
    if (t == WOW_TOKEN_PLAYER || t == WOW_TOKEN_MOUSEOVER) return 1;
    return 0;
}
static int h_dead(void *c, int t) { const Host *h = (const Host *)c; return t == WOW_TOKEN_TARGET ? h->w.target_dead : 0; }
static int h_hostile(void *c, int t) { const Host *h = (const Host *)c; return t == WOW_TOKEN_TARGET ? h->w.target_hostile : 0; }
static int h_cast(void *c, const char *name, int token) {
    Host *h = (Host *)c;
    size_t n = strlen(name);
    if (n >= sizeof h->spell) n = sizeof h->spell - 1;
    memcpy(h->spell, name, n);
    h->spell[n] = '\0';
    h->casts++;
    h->token = token;
    return 1;
}
static int h_use(void *c, int slot) { Host *h = (Host *)c; h->uses++; h->last_slot = slot; return 1; }
static int h_target(void *c, int token) { Host *h = (Host *)c; h->targets++; h->last_target = token; return 1; }
static void h_clear(void *c) { ((Host *)c)->clears++; }
static int h_focus(void *c, int token) { Host *h = (Host *)c; h->focuses++; h->last_focus = token; return 1; }
static void h_clearfocus(void *c) { ((Host *)c)->clears_focus++; }

static void test_compile(void) {
    WowMacroLine m;
    CHECK(wow_macro_parse("/cast [harm,nodead] Blaster; [@player] Mend", &m) == 1, "the canonical line did not compile");
    CHECK(m.cmd == WOW_CMD_CAST && m.clause_count == 2, "wrong shape: cmd %d clauses %d", m.cmd, m.clause_count);
    CHECK(m.clause[0].require_bits == WOW_COND_HARM, "[harm] is not the HARM bit");
    CHECK(m.clause[0].forbid_bits == WOW_COND_DEAD, "[nodead] is not a FORBID of DEAD");
    CHECK(m.clause[0].token == WOW_TOKEN_NONE, "a clause with no [@token] invented one");
    CHECK(strcmp(m.clause[0].arg, "Blaster") == 0, "argument 0 is '%s'", m.clause[0].arg);
    CHECK(m.clause[1].token == WOW_TOKEN_PLAYER && strcmp(m.clause[1].arg, "Mend") == 0,
          "[@player] did not compile to the PLAYER token with 'Mend'");

    CHECK(wow_macro_parse("cast [harm][nodead] Zap", &m) == 1, "accumulated brackets did not compile");
    CHECK(m.clause_count == 1 && m.clause[0].require_bits == WOW_COND_HARM &&
          m.clause[0].forbid_bits == WOW_COND_DEAD, "two bracket groups did not accumulate");

    CHECK(wow_macro_parse("target [@mouseover]", &m) == 1, "a token-only target did not compile");
    CHECK(m.cmd == WOW_CMD_TARGET && m.clause[0].token == WOW_TOKEN_MOUSEOVER && m.clause[0].arg[0] == '\0',
          "token-only target compiled wrong");

    CHECK(wow_macro_parse("/cleartarget", &m) == 1 && m.clause_count == 1 && m.cmd == WOW_CMD_CLEARTARGET,
          "an argument-free command did not get one clause");

    CHECK(wow_macro_parse("use 13", &m) == 1 && m.cmd == WOW_CMD_USE && strcmp(m.clause[0].arg, "13") == 0,
          "item slots do not compile");

    CHECK(wow_macro_parse("/cast [harm] Blaster", &m) == 1, "leading slash rejected");
    CHECK(wow_macro_parse("   ", &m) == 0, "whitespace is not empty");
    CHECK(wow_macro_parse(NULL, &m) == 0, "null text is not empty");
}

static void test_errors(void) {
    WowMacroLine m;
    char longarg[80], big[220];
    memset(longarg, 'x', sizeof longarg - 1);
    longarg[sizeof longarg - 1] = '\0';
    memset(big, 'a', 200);
    big[200] = '\0';
    CHECK(wow_macro_parse("/fly", &m) == -1, "an unknown command compiled");
    CHECK(wow_macro_parse("cast [harm Blaster", &m) == -2, "an unclosed bracket compiled");
    CHECK(wow_macro_parse("cast [banana] Blaster", &m) == -2, "an unknown condition compiled");
    CHECK(wow_macro_parse("cast [@banana] Blaster", &m) == -2, "an unknown unit token compiled");
    CHECK(wow_macro_parse("cast [harm] A; [harm] B; [harm] C; [harm] D; [harm] E", &m) == -3,
          "a fifth clause was accepted");
    CHECK(wow_macro_parse("cast [harm]", &m) == -5, "a cast with no spell compiled");
    CHECK(wow_macro_parse("cast", &m) == -5, "a bare cast compiled");
    {
        char line[128];
        snprintf(line, sizeof line, "cast %s", longarg);
        CHECK(wow_macro_parse(line, &m) == -4, "an oversized argument was truncated instead of rejected");
    }
    CHECK(wow_macro_parse(big, &m) == -6, "an oversized line was accepted");
}

static void test_resolve(void) {
    WowMacroLine m;
    World w;
    int tok = 0;
    const char *arg = NULL;

    CHECK(wow_macro_parse("cast [harm,nodead] Blaster; [@player] Mend", &m) == 1, "compile failed");
    memset(&w, 0, sizeof w);
    w.target_exists = 1;
    w.target_hostile = 1;
    CHECK(wow_macro_resolve(&m, &w, world_unit, &tok, &arg) == 1 && tok == WOW_TOKEN_TARGET &&
          strcmp(arg, "Blaster") == 0, "a live hostile target did not run the first clause");

    w.target_dead = 1;
    CHECK(wow_macro_resolve(&m, &w, world_unit, &tok, &arg) == 1 && tok == WOW_TOKEN_PLAYER &&
          strcmp(arg, "Mend") == 0, "a dead target did not fall through to [@player]");

    memset(&w, 0, sizeof w);
    CHECK(wow_macro_resolve(&m, &w, world_unit, &tok, &arg) == 1 && tok == WOW_TOKEN_PLAYER,
          "no target did not fall through to [@player]");

    CHECK(wow_macro_parse("cast [noexists] Fallback", &m) == 1, "[noexists] did not compile");
    CHECK(wow_macro_resolve(&m, &w, world_unit, &tok, &arg) == 1 && strcmp(arg, "Fallback") == 0,
          "[noexists] did not hold with no target");
    w.target_exists = 1;
    CHECK(wow_macro_resolve(&m, &w, world_unit, NULL, NULL) == 0, "[noexists] held with a target");

    CHECK(wow_macro_parse("cast [@none] Nothing", &m) == 1, "[@none] did not compile");
    memset(&w, 0, sizeof w);
    CHECK(wow_macro_resolve(&m, &w, world_unit, &tok, NULL) == 1 && tok == WOW_TOKEN_NOBODY,
          "[@none] is not the NOBODY token");

    CHECK(wow_macro_parse("cast [dead] OnlyDead", &m) == 1, "[dead] did not compile");
    w.target_exists = 1;
    w.target_hostile = 1;
    CHECK(wow_macro_resolve(&m, &w, world_unit, NULL, NULL) == 0, "[dead] held for a living target");

    CHECK(wow_macro_parse("target [@mouseover]", &m) == 1, "token target did not compile");
    CHECK(wow_macro_resolve(&m, &w, world_unit, &tok, &arg) == 1 && tok == WOW_TOKEN_MOUSEOVER,
          "a token-only clause did not resolve to its token");
}

static void test_execute(void) {
    WowHostApi api;
    Host h;
    WowMacroLine m;
    memset(&api, 0, sizeof api);
    memset(&h, 0, sizeof h);
    api.unit_exists = h_exists;
    api.unit_dead = h_dead;
    api.unit_hostile = h_hostile;
    api.cast_spell = h_cast;
    api.use_action = h_use;
    api.set_target = h_target;
    api.clear_target = h_clear;
    api.set_focus = h_focus;
    api.clear_focus = h_clearfocus;

    h.w.target_exists = 1;
    h.w.target_hostile = 1;
    CHECK(wow_macro_parse("cast [harm,nodead] Blaster; [@player] Mend", &m) == 1, "compile failed");
    CHECK(wow_macro_execute(&m, &api, &h) == 1, "execution reported no clause");
    CHECK(h.casts == 1 && strcmp(h.spell, "Blaster") == 0 && h.token == WOW_TOKEN_TARGET,
          "the cast did not reach the host as Blaster@target");

    h.w.target_dead = 1;
    CHECK(wow_macro_execute(&m, &api, &h) == 1 && h.casts == 2 && h.token == WOW_TOKEN_PLAYER,
          "a dead target did not route the cast to the player");
    h.w.target_dead = 0;

    CHECK(wow_macro_parse("use 13", &m) == 1 && wow_macro_execute(&m, &api, &h) == 1 && h.last_slot == 13,
          "/use 13 did not reach the host with slot 13");

    CHECK(wow_macro_parse("/target [@mouseover]", &m) == 1 && wow_macro_execute(&m, &api, &h) == 1 &&
          h.targets == 1 && h.last_target == WOW_TOKEN_MOUSEOVER, "targeting did not reach the host");

    CHECK(wow_macro_parse("/cleartarget", &m) == 1 && wow_macro_execute(&m, &api, &h) == 1 && h.clears == 1,
          "cleartarget did not reach the host");

    CHECK(wow_macro_parse("focus [@focus]", &m) == 1, "focus compile");
    CHECK(wow_macro_parse("cast [dead] X", &m) == 1, "compile [dead]");
    h.w.target_dead = 1;
    CHECK(wow_macro_execute(&m, &api, &h) == 1, "[dead] should run for a dead target");
    h.w.target_dead = 0;
    CHECK(wow_macro_execute(&m, &api, &h) == 0, "no clause matched but execution claimed it ran");
    CHECK(wow_macro_execute(NULL, &api, &h) == -1, "a null macro executed");
    CHECK(wow_macro_execute(&m, NULL, &h) == -1, "a null host executed");
    {
        WowHostApi bare;
        memset(&bare, 0, sizeof bare);
        bare.unit_exists = h_exists;
        CHECK(wow_macro_parse("cast Zap", &m) == 1 && wow_macro_execute(&m, &bare, &h) == -1,
              "a host with no cast handler reported success");
    }
    CHECK(strcmp(wow_cmd_name(WOW_CMD_CAST), "cast") == 0 && strcmp(wow_cmd_name(0), "?") == 0,
          "command names are wrong");
}

static void test_weapons(void) {
    int i, j;
    CHECK(WOW_WEAPON_COUNT >= 6, "only %d weapons in the generated table", WOW_WEAPON_COUNT);
    for (i = 0; i < WOW_WEAPON_COUNT; i++) {
        CHECK(wow_weapons[i].name && *wow_weapons[i].name, "unnamed weapon at %d", i);
        for (j = i + 1; j < WOW_WEAPON_COUNT; j++)
            CHECK(wow_weapons[i].id != wow_weapons[j].id, "duplicate weapon id %d", wow_weapons[i].id);
    }
    CHECK(wow_weapon_from_name("knife") == wow_weapons[0].id, "knife is not the first weapon");
    CHECK(wow_weapon_from_name("MAGNUM") == wow_weapon_from_name("magnum"), "weapon names are case-sensitive");
    CHECK(wow_weapon_from_name("magnum") >= 0, "the generated magnum is missing");
    CHECK(wow_weapon_from_name("nope") == -1 && wow_weapon_from_name("") == -1 &&
          wow_weapon_from_name(NULL) == -1, "an unknown weapon name resolved");
    CHECK(wow_weapon_name(0) != NULL && strcmp(wow_weapon_name(0), "?") != 0, "id 0 has no name");
    CHECK(strcmp(wow_weapon_name(WOW_WEAPON_COUNT + 99), "?") == 0, "an unknown id invented a name");
    CHECK(wow_weapon_from_slot(1) == wow_weapons[0].id, "slot 1 is not the first weapon");
    CHECK(wow_weapon_from_slot(0) == -1 && wow_weapon_from_slot(10) == -1, "an invalid slot resolved");
    CHECK(wow_weapon_from_slot(7) == wow_weapon_from_name("flashlight") &&
          wow_weapon_from_slot(8) == wow_weapon_from_name("hammer") &&
          wow_weapon_from_slot(9) == wow_weapon_from_name("spraycan"),
          "slots 7/8/9 disagree with the engine's flashlight/hammer/spraycan");
    CHECK(wow_weapon_from_name("flashlight") >= 0 && wow_weapon_from_name("hammer") >= 0 &&
          wow_weapon_from_name("spraycan") >= 0, "a trailing-comment WPN_ define fell out of the table");
}

static void test_api_contract(void) {
    int i, j;
    static const char *const states[] = { "host", "frame", "event", "todo" };
    CHECK(WOW_API_COUNT >= 25, "the contract lists only %d globals", WOW_API_COUNT);
    for (i = 0; i < WOW_API_COUNT; i++) {
        CHECK(wow_api_globals[i].name && *wow_api_globals[i].name, "an unnamed global at %d", i);
        CHECK(wow_api_globals[i].max_args >= wow_api_globals[i].fixed_args,
              "%s: max args below fixed args", wow_api_globals[i].name);
        for (j = i + 1; j < WOW_API_COUNT; j++)
            CHECK(strcmp(wow_api_globals[i].name, wow_api_globals[j].name) != 0,
                  "duplicate global %s", wow_api_globals[i].name);
        {
            int known = 0, s;
            for (s = 0; s < (int)(sizeof states / sizeof states[0]); s++)
                if (strcmp(wow_api_globals[i].state, states[s]) == 0) known = 1;
            CHECK(known, "%s: unknown state '%s'", wow_api_globals[i].name, wow_api_globals[i].state);
        }
    }
    CHECK(wow_api_find("UnitExists") != NULL, "UnitExists is missing from the contract");
    CHECK(wow_api_find("CastSpellByName") != NULL && wow_api_find("CastSpellByName")->fixed_args == 1,
          "CastSpellByName signature is wrong");
    CHECK(wow_api_find("CreateFrame") != NULL, "CreateFrame is missing");
    CHECK(wow_api_find("GetActionInfo") != NULL, "GetActionInfo is missing");
    CHECK(wow_api_find("UnitCastingInfo") != NULL, "UnitCastingInfo is missing");
    CHECK(wow_api_find("NotARealFunction") == NULL, "the contract invented a global");
    CHECK(wow_api_find(NULL) == NULL, "a null lookup matched");
}

static const char *TOC_SAMPLE =
    "## Interface: 20261103\n"
    "## Title: SHANKPIT Target Frames\n"
    "## Author: Emily Springerton\n"
    "## Version: 1.4.60\n"
    "## SavedVariables: SHANKPIT_FramePositions\n"
    "## Dependencies: SHANKPIT_Core, PELLEGRINO_EmbedUI\n"
    "## OptionalDependencies: GOLDENBAND_Debug\n"
    "## X-NewerClientKey: whatever\n"
    "# a comment\n"
    "\n"
    "Bindings.xml\n"
    "Localization\\enUS.lua\n"
    "TargetFrameLogic.lua\n";

static void test_toc(void) {
    AddonToc t;
    memset(&t, 0, sizeof t);
    toc_copy(t.name, sizeof t.name, "SHANKPIT_UnitFrames");
    CHECK(addon_toc_parse(TOC_SAMPLE, &t) == 3, "expected 3 load files, got %d", t.file_count);
    CHECK(t.interface_version == 20261103UL, "interface version is %lu", t.interface_version);
    CHECK(strcmp(t.title, "SHANKPIT Target Frames") == 0, "title is '%s'", t.title);
    CHECK(strcmp(t.author, "Emily Springerton") == 0 && strcmp(t.version, "1.4.60") == 0, "author/version wrong");
    CHECK(strcmp(t.saved_variables, "SHANKPIT_FramePositions") == 0, "saved variables not captured");
    CHECK(t.dep_count == 2 && strcmp(t.deps[1], "PELLEGRINO_EmbedUI") == 0, "dependencies are wrong");
    CHECK(t.optional_count == 1 && strcmp(t.optional[0], "GOLDENBAND_Debug") == 0, "optional deps are wrong");
    CHECK(strcmp(t.files[0], "Bindings.xml") == 0 && strcmp(t.files[2], "TargetFrameLogic.lua") == 0 &&
          strcmp(t.files[1], "Localization\\enUS.lua") == 0, "load order was not preserved");
    CHECK(t.bad_lines == 0, "a valid file reported %d bad directive lines", t.bad_lines);

    memset(&t, 0, sizeof t);
    CHECK(addon_toc_parse("## Dependencies:\n## BrokenDirective\n", &t) == 0 && t.bad_lines == 1,
          "a directive with no ':' was not reported");
    CHECK(addon_toc_parse(NULL, &t) == 0, "null text was not empty");
    CHECK(addon_toc_parse("", &t) == 0, "empty text was not empty");
}

static void test_toc_order(void) {
    AddonToc list[3];
    int order[3], bad = 0;
    memset(list, 0, sizeof list);
    toc_copy(list[0].name, TOC_NAME_MAX, "A");
    toc_copy(list[1].name, TOC_NAME_MAX, "B");
    toc_copy(list[2].name, TOC_NAME_MAX, "C");
    toc_copy(list[0].deps[0], TOC_NAME_MAX, "B"); list[0].dep_count = 1;
    toc_copy(list[1].deps[0], TOC_NAME_MAX, "C"); list[1].dep_count = 1;

    CHECK(addon_toc_order(list, 3, order, &bad) == 3, "ordering did not return every add-on");
    CHECK(strcmp(list[order[0]].name, "C") == 0 && strcmp(list[order[1]].name, "B") == 0 &&
          strcmp(list[order[2]].name, "A") == 0, "dependency order is wrong: %d,%d,%d",
          order[0], order[1], order[2]);

    toc_copy(list[2].deps[0], TOC_NAME_MAX, "A"); list[2].dep_count = 1;
    CHECK(addon_toc_order(list, 3, order, &bad) == -2 && bad >= 0, "a cycle was not reported");
    list[2].dep_count = 0;

    toc_copy(list[1].deps[0], TOC_NAME_MAX, "NOT_INSTALLED");
    CHECK(addon_toc_order(list, 3, order, &bad) == -1 && bad == 1, "a missing dependency was not reported");
    CHECK(addon_toc_order(NULL, 3, order, &bad) == -1 && addon_toc_order(list, 0, order, &bad) == 0 &&
          addon_toc_order(list, TOC_ADDONS_MAX + 1, order, &bad) == -1, "bounds are wrong");

    {
        char many[256];
        AddonToc t;
        size_t i;
        memset(&t, 0, sizeof t);
        snprintf(many, sizeof many, "## Dependencies:");
        for (i = 0; i < TOC_DEPS_MAX + 2; i++) {
            size_t n = strlen(many);
            snprintf(many + n, sizeof many - n, " D%d", (int)i);
        }
        snprintf(many + strlen(many), sizeof many - strlen(many), "\n");
        CHECK(addon_toc_parse(many, &t) == -1, "an oversized dependency list was truncated, not rejected");
    }
}

int main(void) {
    test_compile();
    test_errors();
    test_resolve();
    test_execute();
    test_weapons();
    test_api_contract();
    test_toc();
    test_toc_order();
    if (fails) { printf("wow: %d FAILED\n", fails); return 1; }
    printf("wow: all checks passed\n");
    return 0;
}
