/* anim_state.h -- which animation clip a player is playing, for an imported N-joint skin.
 *
 * The decision is PARENA's (PARENA/stdlib/shankpit/anim_rules.prn, generated into anim_rules.c by
 * `make regen-anim-rules`); this header is the host: it reads the engine's real PlayerState fields,
 * maps protocol.h's weapon ids onto the three melee classes, and publishes the clip-name contract an
 * imported Mixamo/GBAND bundle has to satisfy.
 *
 * WHY A TABLE OF NAMES: the spec asks an imported skin to "bundle or map to" thirteen sequences.
 * GOLDENBAND clips are channel-name-blind (gband.h) and the kit loader takes clip names as strings
 * (gband_skel_npc_load_kit), so the contract is a fixed list of names -- an importer renames its
 * Mixamo clips to these, or it fails loudly at load instead of silently playing the idle for
 * everything. The names are lowercase and hyphen-free because they become file stems
 * (<asset_dir>/<name>.gband + .gband.json).
 *
 * The loaded half now exists: packages/simulation/anim_clipset.h loads these thirteen names through
 * GOLDENBAND's own loader, ticks them (looping postures, holding one-shots) and falls back to a clip
 * that exists when a bundle has holes. tools/mixamo_clipset.py produces that bundle from real
 * Mixamo-derived .gband clips and refuses one that would fail at load.
 *
 * DRAWN NOW: a kit loaded from an imported clipset carries all thirteen states
 * (gband_skel_npc_draw_state, packages/goldenband/gband_skel_npc.c), and main.c drives it with this
 * header's own anim_state_for_player, so an imported skin plays run, crouch-walk, jump, in-air, fire,
 * reload, swing, ability and climb rather than idle/walk and two gestures standing in for four. The
 * built-in characters are still 4-clip kits and keep that path; and a SKIN still has to be an imported
 * clipset to have the thirteen (a built-in cosmetic has no bundle to load).
 *
 * The two clip-name lists (this header's and the kit loader's) are separate on purpose -- the goldenband
 * package does not depend on this one -- and `make test-anim-states` asserts they agree string for string.
 * Still not done, named: the thirteen are selected per PLAYER, not per NPC brain state, so a greeted NPC
 * still uses the anim_override path for its wave rather than the state machine.
 */
#ifndef SHANKPIT_ANIM_STATE_H
#define SHANKPIT_ANIM_STATE_H

#include "protocol.h"

/* State ids, mirroring anim_rules.prn's own doc comment and table order. */
enum {
    ANIM_IDLE = 0, ANIM_WALK, ANIM_RUN, ANIM_CROUCH_IDLE, ANIM_CROUCH_WALK,
    ANIM_JUMP_START, ANIM_IN_AIR, ANIM_SLIDE, ANIM_WEAPON_FIRE, ANIM_RELOAD,
    ANIM_MELEE_SWING, ANIM_ABILITY, ANIM_CLIMB_LOOP, ANIM_STATE_COUNT
};

/* Melee classes (the only three weapons with a swing window). */
enum { ANIM_MELEE_LIGHT = 0, ANIM_MELEE_KATANA = 1, ANIM_MELEE_HAMMER = 2, ANIM_MELEE_NONE = 3 };

/* ---- PARENA-generated (anim_rules.c) ---- */
int anim_state_count(void);
int anim_state_select(int grounded, int crouching, int speed_milli, int jump_ticks, int slide_ticks,
                      int fire_ticks, int reload_ticks, int melee_ticks, int ability_ticks, int climb);
int anim_state_loops(int state);
int anim_state_additive(int state);
int anim_state_blend_ms(int state);
int anim_speed_band(int speed_milli);
int anim_melee_ticks(int kind);
int anim_eye_height_mm(int crouching);
int anim_walk_speed_milli(void);
int anim_run_speed_milli(void);
int anim_jump_start_ticks(void);
int anim_melee_kind_light(void);
int anim_melee_kind_katana(void);
int anim_melee_kind_hammer(void);

/* ---- the clip-name contract ------------------------------------------------------------------- */
static inline const char *anim_state_clip_name(int state) {
    static const char *const names[ANIM_STATE_COUNT] = {
        "anim_idle", "anim_walk", "anim_run", "anim_crouch_idle", "anim_crouch_walk",
        "anim_jump_start", "anim_in_air", "anim_slide", "anim_weapon_fire", "anim_reload",
        "anim_melee_swing", "anim_ability", "anim_climb_loop"
    };
    if (state < 0 || state >= ANIM_STATE_COUNT) return "anim_idle";
    return names[state];
}

/* Which melee class a protocol.h weapon is: the three with a real swing window, everything else none. */
static inline int anim_state_melee_kind(int weapon) {
    if (weapon == WPN_KNIFE) return anim_melee_kind_light();
    if (weapon == WPN_KATANA) return anim_melee_kind_katana();
    if (weapon == WPN_HAMMER) return anim_melee_kind_hammer();
    return ANIM_MELEE_NONE;
}

/* The melee window in ticks for a weapon (0 = this weapon has no swing). */
static inline int anim_state_melee_ticks(int weapon) {
    int kind = anim_state_melee_kind(weapon);
    if (kind == ANIM_MELEE_NONE) return 0;
    return anim_melee_ticks(kind);
}

/* Horizontal speed in units/tick x 1000 -- the same unit the rules' speed bands use.
 * sqrt on the squared sum, so a diagonal run is not 1.41x faster than a straight one. */
static inline int anim_state_speed_milli(const PlayerState *p) {
    float vx, vz, v2;
    if (!p) return 0;
    vx = p->vx; vz = p->vz;
    v2 = vx * vx + vz * vz;
    if (v2 <= 0.0f) return 0;
    return (int)(__builtin_sqrtf(v2) * 1000.0f);
}

/* anim_state_for_player -- the one call a renderer needs: the clip to play for this tick.
 *
 * Reads only fields that exist on the real struct: on_ground, crouching, vx/vz, jump_timer,
 * is_shooting, reload_timer, katana_slash_timer, dash_timer, current_weapon.
 *
 * `slide_ticks` is passed in by the caller (0 if it has none) because THIS ENGINE HAS NO SLIDE
 * TIMER: physics.h's crouch-slide is a friction change and the wall ride is ctrl-in-air, and
 * neither sets one. Rather than invent a timer in a header, the slot is explicit and the state is
 * reachable the moment a real producer exists. */
static inline int anim_state_for_player(const PlayerState *p, int slide_ticks, int climb) {
    if (!p) return ANIM_IDLE;
    return anim_state_select(p->on_ground ? 1 : 0,
                             p->crouching ? 1 : 0,
                             anim_state_speed_milli(p),
                             p->jump_timer,
                             slide_ticks,
                             p->is_shooting,
                             p->reload_timer,
                             p->katana_slash_timer,
                             p->dash_timer,
                             climb);
}

#endif /* SHANKPIT_ANIM_STATE_H */
