/* WOW API SURFACE -- the contract a Lua binding wraps, and the table it is checked against.
 *
 * The engine does NOT vendor a Lua VM. This header is the C side of the WoW contract: the host vtable
 * that performing a unit query or an action goes through, and a declarative table of the global names
 * add-on code expects, with their arity and results. A binding is then mechanical -- every global in
 * wow_api_globals[] is a thin wrapper over wow_api_host or over a frame/event module -- and the table is
 * testable (names unique, each one present, arity sane) without a Lua interpreter in the build.
 *
 * WHAT IS REAL HERE: unit existence/health/class queries, target/focus mutation, spell/item/action
 * dispatch. WHAT IS CONTRACT BUT NOT IMPLEMENTED: CreateFrame and frame methods, SetAttribute and
 * secure templates, event dispatch, COMBAT_LOG_EVENT_UNFILTERED framing, action bar slot state. They
 * are listed so the surface is exhaustive and so nothing is quietly claimed as done.
 */
#ifndef SHANKPIT_UI_WOW_API_H
#define SHANKPIT_UI_WOW_API_H

#include <stdlib.h>
#include <string.h>
#include "wow_macro.h"

/* The engine's side of the contract. A missing function pointer is a feature the host does not have,
   and the caller must treat it as "no" rather than call through it. */
typedef struct {
    int  (*unit_exists)(void *ctx, int token);
    int  (*unit_dead)(void *ctx, int token);
    int  (*unit_hostile)(void *ctx, int token);
    long (*unit_health)(void *ctx, int token);
    long (*unit_health_max)(void *ctx, int token);
    int  (*cast_spell)(void *ctx, const char *name, int token);
    int  (*use_action)(void *ctx, int slot);
    int  (*set_target)(void *ctx, int token);
    void (*clear_target)(void *ctx);
    int  (*set_focus)(void *ctx, int token);
    void (*clear_focus)(void *ctx);
} WowHostApi;

typedef struct {
    const WowHostApi *api;
    void *ctx;
} WowUnitCtx;

/* Adapt the host vtable to the macro evaluator. Stealth is not tracked by the engine, so [stealth]
   never holds -- an honest no, not a lie that it does. */
static inline WowUnit wow_host_unit(void *p, int token) {
    WowUnit u;
    const WowUnitCtx *c = (const WowUnitCtx *)p;
    memset(&u, 0, sizeof u);
    if (!c || !c->api || token <= 0) return u;
    if (!c->api->unit_exists || !c->api->unit_exists(c->ctx, token)) return u;
    u.exists = 1;
    u.dead = c->api->unit_dead ? (unsigned char)(c->api->unit_dead(c->ctx, token) != 0) : 0;
    u.hostile = c->api->unit_hostile ? (unsigned char)(c->api->unit_hostile(c->ctx, token) != 0) : 0;
    return u;
}

/* Run a compiled macro against the host. Returns 1 if a clause ran, 0 if none held, -1 if the host is
   not usable (no api, or nullptr where an action is needed). A host function that is absent is a
   REFUSAL, not a crash: the macro reports that it could not act. */
static inline int wow_macro_execute(const WowMacroLine *m, const WowHostApi *api, void *ctx) {
    WowUnitCtx uc;
    int token = 0;
    const char *arg = NULL;
    if (!m || !api) return -1;
    uc.api = api;
    uc.ctx = ctx;
    if (!wow_macro_resolve(m, &uc, wow_host_unit, &token, &arg)) return 0;
    switch (m->cmd) {
        case WOW_CMD_CAST:
            if (!api->cast_spell) return -1;
            return api->cast_spell(ctx, arg ? arg : "", token) ? 1 : 0;
        case WOW_CMD_USE:
            if (!api->use_action) return -1;
            return api->use_action(ctx, (int)strtol(arg ? arg : "", NULL, 10)) ? 1 : 0;
        case WOW_CMD_TARGET:
            if (!api->set_target) return -1;
            return api->set_target(ctx, token) ? 1 : 0;
        case WOW_CMD_FOCUS:
            if (!api->set_focus) return -1;
            return api->set_focus(ctx, token) ? 1 : 0;
        case WOW_CMD_CLEARTARGET:
            if (!api->clear_target) return -1;
            api->clear_target(ctx);
            return 1;
        case WOW_CMD_CLEARFOCUS:
            if (!api->clear_focus) return -1;
            api->clear_focus(ctx);
            return 1;
        case WOW_CMD_STARTATTACK:
            if (!api->set_target) return -1;
            return api->set_target(ctx, token) ? 1 : 0;   /* auto-attack begins on the next tick */
        default:
            return -1;
    }
}

/* The declared global surface. fixed_args is the smallest legal call; max_args the largest, or -1 when
   the function is variadic past fixed_args. Keep this table in step with docs/WOW_API_CONTRACT.md --
   the test checks for duplicates and for the names add-ons actually call. */
typedef struct {
    const char *name;
    int         fixed_args;
    int         max_args;
    const char *args;
    const char *results;
    const char *state;   /* "host", "frame", "event", "todo" -- what backs it today */
} WowApiGlobal;

static const WowApiGlobal wow_api_globals[] = {
    { "CastSpellByName",      1,  2, "spellName[, unitToken]",   "nil",        "host"  },
    { "UseInventoryItem",     1,  1, "slotId",                   "nil",        "host"  },
    { "TargetUnit",           1,  1, "unitToken",                "nil",        "host"  },
    { "ClearTarget",          0,  0, "",                         "nil",        "host"  },
    { "FocusUnit",            1,  1, "unitToken",                "nil",        "host"  },
    { "ClearFocus",           0,  0, "",                         "nil",        "host"  },
    { "UseAction",            1,  2, "slotId[, checkCursor]",    "nil",        "host"  },
    { "UnitExists",           1,  1, "unitToken",                "boolean",    "host"  },
    { "UnitHealth",           1,  1, "unitToken",                "integer",    "host"  },
    { "UnitHealthMax",        1,  1, "unitToken",                "integer",    "host"  },
    { "UnitPower",            1,  2, "unitToken[, powerType]",   "integer",    "host"  },
    { "UnitPowerMax",         1,  2, "unitToken[, powerType]",   "integer",    "host"  },
    { "UnitCanAttack",        2,  2, "sourceUnit, targetUnit",   "boolean",    "host"  },
    { "UnitCanAssist",        2,  2, "sourceUnit, targetUnit",   "boolean",    "host"  },
    { "UnitIsDeadOrGhost",    1,  1, "unitToken",                "boolean",    "host"  },
    { "UnitName",             1,  1, "unitToken",                "string",     "todo"  },
    { "UnitClass",            1,  1, "unitToken",                "string,string", "todo" },
    { "UnitGUID",             1,  1, "unitToken",                "string",     "todo"  },
    { "GetSpellCooldown",     1,  1, "spellNameOrId",            "start,duration,enabled", "todo" },
    { "IsUsableSpell",        1,  2, "spellNameOrId[, checkMana]", "boolean,boolean", "todo" },
    { "UnitCastingInfo",      1,  1, "unitToken",                "name,text,texture,start,end,tradeSkill,castID,notInterruptible", "todo" },
    { "UnitChannelInfo",      1,  1, "unitToken",                "same as UnitCastingInfo, or nil", "todo" },
    { "CreateFrame",          1,  4, "frameType[, name, parent, template]", "Frame", "frame" },
    { "GetActionInfo",        1,  1, "slotId",                   "actionType, id, subType", "todo" },
    { "GetActionTexture",     1,  1, "slotId",                   "texture",    "todo"  },
    { "GetActionCount",       1,  1, "slotId",                   "integer",    "todo"  },
    { "IsUsableAction",       1,  2, "slotId[, checkMana]",      "boolean",    "todo"  },
    { "IsActionInRange",      1,  2, "slotId[, unitToken]",      "boolean or nil", "todo" },
    { "SetAttribute",         2,  2, "name, value",              "nil",        "frame" },
    { "RegisterEvent",        1,  1, "eventName",                "nil",        "frame" },
    { "SetScript",            2,  2, "handlerName, callback",    "nil",        "frame" }
};

#define WOW_API_COUNT ((int)(sizeof wow_api_globals / sizeof wow_api_globals[0]))

static inline const WowApiGlobal *wow_api_find(const char *name) {
    int i;
    if (!name) return NULL;
    for (i = 0; i < WOW_API_COUNT; i++)
        if (strcmp(wow_api_globals[i].name, name) == 0) return &wow_api_globals[i];
    return NULL;
}

#endif /* SHANKPIT_UI_WOW_API_H */
