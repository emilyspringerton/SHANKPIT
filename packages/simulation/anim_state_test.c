/* anim_state_test.c -- the clip decision, headless.
 *
 * Checks the three things that actually go wrong in an animation state machine: the wrong clip wins
 * a priority fight (a player shooting while airborne must still shoot), a loop/one-shot flag is
 * backwards (an idle that plays once, a reload that repeats forever), and the clip contract drifts
 * from the state set (a state with no name is invisible, two states with one name are the same clip).
 *
 * The eye-height check deliberately compares against physics.h's real EYE_HEIGHT, not a literal:
 * the spec's 4.0/2.5 numbers belong to another build, and the point of asserting is that this
 * engine's camera height and the pose height cannot drift apart silently.
 */
#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "anim_state.h"

/* the engine's real EYE_HEIGHT, from the one TU that includes physics.h (anim_engine_constant.c) */
int anim_engine_eye_height_mm(void);

static int g_fail = 0, g_checks = 0;
static void ok(int cond, const char *what) {
    g_checks++;
    if (!cond) { g_fail++; printf("FAIL  %s\n", what); }
    else printf("ok    %s\n", what);
}

/* grounded, crouch, speed, jump, slide, fire, reload, melee, ability, climb */
static int sel(int g, int c, int sp, int j, int sl, int f, int r, int m, int a, int cl) {
    return anim_state_select(g, c, sp, j, sl, f, r, m, a, cl);
}

int main(void) {
    /* ---- the state set and its clip contract -------------------------------------------------- */
    ok(anim_state_count() == ANIM_STATE_COUNT, "PARENA's state count equals the header's enum");
    ok(ANIM_STATE_COUNT == 13, "thirteen states, as the spec's animation set lists");

    {
        int dupes = 0, empty = 0;
        for (int i = 0; i < ANIM_STATE_COUNT; i++) {
            const char *n = anim_state_clip_name(i);
            if (!n || !n[0]) empty++;
            for (int j = i + 1; j < ANIM_STATE_COUNT; j++)
                if (n && strcmp(n, anim_state_clip_name(j)) == 0) dupes++;
        }
        ok(empty == 0, "every state has a clip name (no invisible state)");
        ok(dupes == 0, "no two states share a clip name");
        ok(strcmp(anim_state_clip_name(-1), "anim_idle") == 0 &&
           strcmp(anim_state_clip_name(99), "anim_idle") == 0,
           "an out-of-range state falls back to idle rather than a null name");
    }

    /* ---- locomotion -------------------------------------------------------------------------- */
    ok(sel(1, 0, 0, 0, 0, 0, 0, 0, 0, 0) == ANIM_IDLE, "standing still on the ground is IDLE");
    ok(sel(1, 0, anim_walk_speed_milli() - 1, 0, 0, 0, 0, 0, 0, 0) == ANIM_IDLE,
       "one unit/tick below the walk band is still IDLE");
    ok(sel(1, 0, anim_walk_speed_milli(), 0, 0, 0, 0, 0, 0, 0) == ANIM_WALK,
       "the walk band starts exactly at its threshold");
    ok(sel(1, 0, anim_run_speed_milli() - 1, 0, 0, 0, 0, 0, 0, 0) == ANIM_WALK,
       "top walking speed is WALK, not RUN");
    ok(sel(1, 0, anim_run_speed_milli(), 0, 0, 0, 0, 0, 0, 0) == ANIM_RUN,
       "the run band starts exactly at its threshold");
    ok(sel(1, 0, -500, 0, 0, 0, 0, 0, 0, 0) == ANIM_RUN,
       "moving backwards fast is RUN, not IDLE (speed is unsigned)");
    ok(sel(1, 1, 900, 0, 0, 0, 0, 0, 0, 0) == ANIM_CROUCH_IDLE,
       "crouching at speed still reads CROUCH: the crouch clips own locomotion");
    ok(sel(1, 1, 0, 0, 1, 0, 0, 0, 0, 0) == ANIM_SLIDE,
       "a live slide timer outranks the crouch idle");

    /* ---- air --------------------------------------------------------------------------------- */
    ok(sel(0, 0, 400, 1, 0, 0, 0, 0, 0, 0) == ANIM_JUMP_START, "tick 1 of a jump is JUMP_START");
    ok(sel(0, 0, 400, anim_jump_start_ticks(), 0, 0, 0, 0, 0, 0) == ANIM_JUMP_START,
       "the jump start lasts its own full window");
    ok(sel(0, 0, 400, anim_jump_start_ticks() + 1, 0, 0, 0, 0, 0, 0) == ANIM_IN_AIR,
       "one tick past it is IN_AIR, not a second jump start");
    ok(sel(0, 0, 400, 0, 0, 0, 0, 0, 0, 0) == ANIM_IN_AIR,
       "falling with no jump (a walk-off) is IN_AIR immediately");
    ok(sel(0, 0, 400, 0, 0, 0, 0, 0, 0, 1) == ANIM_CLIMB_LOOP,
       "the reserved climb state is reachable when a host has a real slope producer");
    ok(sel(0, 1, 400, 0, 1, 0, 0, 0, 0, 0) == ANIM_IN_AIR,
       "a slide timer does not apply in the air");

    /* ---- priority ---------------------------------------------------------------------------- */
    ok(sel(1, 0, 100, 0, 0, 0, 0, 20, 0, 0) == ANIM_MELEE_SWING, "a swing window plays MELEE_SWING");
    ok(sel(1, 0, 100, 0, 0, 4, 30, 20, 0, 0) == ANIM_MELEE_SWING,
       "a swing outranks both firing and reloading");
    ok(sel(0, 0, 400, 3, 0, 4, 0, 0, 0, 0) == ANIM_WEAPON_FIRE,
       "firing while airborne still fires (the recoil is additive)");
    ok(sel(1, 0, 100, 0, 0, 0, 30, 0, 0, 0) == ANIM_RELOAD,
       "reloading outranks walking");
    ok(sel(1, 0, 900, 0, 0, 0, 0, 0, 10, 0) == ANIM_ABILITY, "an ability window plays ABILITY");
    ok(sel(1, 1, 0, 0, 1, 0, 0, 0, 10, 0) == ANIM_ABILITY,
       "an ability outranks a slide (a committed action wins)");
    ok(sel(1, 0, 100, 0, 0, 0, 0, 0, 0, 0) == ANIM_WALK,
       "a zero timer is not a combat state -- walking speed still reads WALK");
    ok(sel(1, 0, 0, 0, 0, 0, 0, 0, 0, 0) == ANIM_IDLE,
       "and no timer with no movement is plain IDLE");

    /* ---- loop / additive / blend flags -------------------------------------------------------- */
    {
        int looped = 0, oneshot = 0;
        for (int s = 0; s < ANIM_STATE_COUNT; s++) {
            int l = anim_state_loops(s), a = anim_state_additive(s), b = anim_state_blend_ms(s);
            ok(l == 0 || l == 1, "loop flag is a flag");
            ok(a == 0 || a == 1, "additive flag is a flag");
            ok(b > 0 && b <= 1000, "blend time is a positive, sane millisecond count");
            if (l) looped++; else oneshot++;
        }
        printf("      %d looping states, %d one-shot states\n", looped, oneshot);
        ok(looped == 6 && oneshot == 7,
           "exactly the six postures loop and the seven actions do not");
        {
            /* every looping state must be a posture -- a looping swing or reload is the classic bug */
            int posture_loops = anim_state_loops(ANIM_IDLE) + anim_state_loops(ANIM_WALK) +
                                anim_state_loops(ANIM_RUN) + anim_state_loops(ANIM_CROUCH_IDLE) +
                                anim_state_loops(ANIM_CROUCH_WALK) + anim_state_loops(ANIM_IN_AIR);
            ok(posture_loops == 6 && looped == 6,
               "the only looping states are the six postures (no looping action slipped in)");
        }
    }
    ok(anim_state_loops(ANIM_IDLE) == 1 && anim_state_loops(ANIM_RUN) == 1 &&
       anim_state_loops(ANIM_CROUCH_IDLE) == 1 && anim_state_loops(ANIM_IN_AIR) == 1,
       "the postures loop");
    ok(anim_state_loops(ANIM_JUMP_START) == 0 && anim_state_loops(ANIM_SLIDE) == 0 &&
       anim_state_loops(ANIM_RELOAD) == 0 && anim_state_loops(ANIM_MELEE_SWING) == 0,
       "the one-shots do not");
    ok(anim_state_additive(ANIM_WEAPON_FIRE) == 1, "only the recoil is upper-body additive");
    ok(anim_state_additive(ANIM_RELOAD) == 0, "a reload owns both hands");
    ok(anim_state_blend_ms(ANIM_WEAPON_FIRE) < anim_state_blend_ms(ANIM_IDLE),
       "a shot blends in faster than a posture");

    /* ---- the engine's own numbers ------------------------------------------------------------- */
    {
        int knife = anim_state_melee_ticks(WPN_KNIFE);
        int katana = anim_state_melee_ticks(WPN_KATANA);
        int hammer = anim_state_melee_ticks(WPN_HAMMER);
        int ar = anim_state_melee_ticks(WPN_AR);
        printf("      melee windows: knife %d, katana %d, hammer %d, rifle %d ticks\n",
               knife, katana, hammer, ar);
        ok(knife == 20, "the knife swings for 20 ticks");
        ok(katana == 28, "the katana swings for 28 ticks");
        ok(hammer == 42, "the demolition hammer swings for 42 ticks");
        ok(ar == 0, "a rifle has no swing window at all");
        ok(knife > 0 && hammer > knife, "the heavy weapon's window is the longest");

        /* the reload the engine actually runs, both flavours */
        ok(RELOAD_TIME_FULL == 60 && RELOAD_TIME_TACTICAL == 42,
           "the rule's reload windows still match protocol.h's constants");

        int stand = anim_eye_height_mm(0), crouch = anim_eye_height_mm(1);
        int engine = anim_engine_eye_height_mm();
        printf("      eye height: standing %d mm (engine EYE_HEIGHT %d mm), crouched %d mm\n",
               stand, engine, crouch);
        ok(stand == engine, "standing eye height IS the engine's EYE_HEIGHT");
        ok(crouch < stand, "crouching lowers the eye, so the pose and the camera agree");
    }

    /* ---- the player wrapper reads the real struct --------------------------------------------- */
    {
        PlayerState p;
        memset(&p, 0, sizeof(p));
        p.on_ground = 1;
        ok(anim_state_for_player(&p, 0, 0) == ANIM_IDLE, "a freshly spawned player is IDLE");

        p.vx = 0.5f;
        ok(anim_state_for_player(&p, 0, 0) == ANIM_RUN, "0.5 units/tick reads as RUN");
        p.crouching = 1;
        ok(anim_state_for_player(&p, 0, 0) == ANIM_CROUCH_IDLE, "crouching overrides the run");

        p.crouching = 0; p.vx = 0.0f;
        p.is_shooting = 3;
        ok(anim_state_for_player(&p, 0, 0) == ANIM_WEAPON_FIRE, "is_shooting drives WEAPON_FIRE");

        p.is_shooting = 0;
        p.reload_timer = 60;
        ok(anim_state_for_player(&p, 0, 0) == ANIM_RELOAD, "reload_timer drives RELOAD");

        p.reload_timer = 0;
        p.current_weapon = WPN_HAMMER;
        p.katana_slash_timer = anim_state_melee_ticks(WPN_HAMMER);
        ok(anim_state_for_player(&p, 0, 0) == ANIM_MELEE_SWING,
           "a hammer's own swing timer plays MELEE_SWING");

        p.katana_slash_timer = 0;
        p.on_ground = 0;
        p.jump_timer = 1;
        ok(anim_state_for_player(&p, 0, 0) == ANIM_JUMP_START,
           "jump_timer drives JUMP_START off the ground");

        ok(anim_state_for_player(NULL, 0, 0) == ANIM_IDLE, "a null player is IDLE, not a crash");

        /* a diagonal is not faster than a straight line */
        p.vx = 0.3f; p.vz = 0.3f;
        {
            int diag = anim_state_speed_milli(&p);
            printf("      speed of (0.3, 0.3) units/tick = %d milli-units/tick\n", diag);
            ok(diag > 400 && diag < 430, "0.3 diagonally is ~0.424 units/tick, not 0.6");
        }
    }

    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
