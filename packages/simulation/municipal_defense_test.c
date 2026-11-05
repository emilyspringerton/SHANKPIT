/* municipal_defense_test.c -- "prove the clerks fight": the civic district's twelve bodies, spawned out
 * of the exported city, driven by the engine's real story_ai brain against a player standing in their
 * own rooms.
 *
 * What was unnamed before this file: the district shipped with two bodies per building standing in the
 * right places, and every test asserted THEIR POSITIONS. Nothing ever ticked them. A body can be placed
 * perfectly, spawn into a real slot, and still stand there for the life of the map -- a civic building
 * full of clerks who never notice you is exactly the "authored, loaded, never spawned" failure the
 * staffing split exists to prevent, one step further along.
 *
 * So this does the four things a player would test by walking in:
 *   1. a body takes the ENGAGE order and pulls the trigger when the player is inside its building;
 *   2. it turns to face the player rather than firing at whatever it was already looking at;
 *   3. it closes the distance when the player backs off down the range;
 *   4. and the shot that comes out of it LANDS -- the player's health actually drops.
 *
 * The district's staff are seven different jobs (BUILDING_STAFF in tools/build_municipal_district.py):
 * a shotgun bailiff, an AR sergeant, a sniper archivist, an AR officer, a knife orderly, a leashed
 * shotgun warden -- and, behind the courthouse's bailiff, the civic boss. The last sections report every
 * body's archetype, brain role, kit and weapon, so a district that quietly went back to twelve identical
 * clerks fails on the distinct-jobs assertion rather than passing as "one working building, six times".
 *
 * The boss gets its own section. It is AI_ROLE_MAGISTRATE: the one body in the district carrying
 * WPN_MAGNUM, the one that holds its range instead of charging, and the one whose ability writes a real
 * stun deadline onto the player (stunned_until_ms, the field local_game.h's own input gate reads). A
 * boss that only had a bigger health bar would be a clerk with a title; these are the three things a
 * player can actually feel, so these are the three the test measures. The verdict is also TELEGRAPHED:
 * the bench arms a 600 ms window before the sentence lands, and breaking the line inside it drops the
 * sentence entirely. The test walks that window from the AI's own tell state -- the same
 * story_ai_verdict_tell_fill the client's world ring and boss bar read -- so the tell cannot drift into
 * decoration painted beside a rule that ignores it.
 *
 * The twelve are identified from the level's OWN municipal_city.characters record and matched against
 * what level_boxes.h actually loaded, so a tool that appends the wrong bodies (or writes a record that
 * disagrees with the array) fails here rather than in a playtest.
 *
 * The last section closes the gap the first version of this file named and left open: intent is not
 * damage. local_game.h's update_entity runs update_weapons (physics.h) for EVERY active slot each tick
 * with that slot's own brain-written in_shoot as the trigger, so a clerk's shot goes through exactly
 * the path the player's trigger does: hitscan/melee -> check_hit_location -> katana_apply_damage ->
 * health. This file makes that same call -- same function, same arguments, same per-slot order as the
 * host's boundary -- and measures the hero's health.
 *
 * HONEST LIMIT, still: this is a headless process. There is no socket, no snapshot, no client and no
 * presentation here, so what is proven is the simulation's own damage path, not a live match's. The
 * magistrate's ring and boss bar are drawn in apps/lobby and are not exercised here at all -- what this
 * file measures is the state they draw from, and the bar itself is the one thing in this section that
 * has never been seen on a screen (no display on this box).
 *
 * Build/run: make test-municipal-defense
 */
#include "story_ai.h"
#include "empire_units.h"
#include "empire_units_rules_host.h"
#include "../common/protocol.h"
#include "../common/physics.h"   /* the real update_weapons/check_hit_location/katana_apply_damage */
#include "../common/shared_movement.h"
#include "../world/level_boxes.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* story_ai.c seeds the BIG_O witness roster for VOXWORLD; stubbed here so this stays a test of the
   district rather than of the citizen sim (loft_city_test.c stubs it for the same reason). */
void witness_ai_seed_voxworld_encounter(ServerState *s, unsigned int now_ms) { (void)s; (void)now_ms; }

/* physics.h's phys_respawn calls into local_game.h's bot evolution; no respawn here (the same two
   stubs magnum_spray_test.c links against physics.h needs). */
PlayerState *get_best_bot(void) { return NULL; }
void evolve_bot(PlayerState *a, PlayerState *b) { (void)a; (void)b; }

static const char *CITY_PATH = "var/zombie/nextown_zombies.json";

static int g_checks = 0;
static int g_failures = 0;

static void ok(int cond, const char *what) {
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("FAIL  %s\n", what);
    } else {
        printf("ok    %s\n", what);
    }
}

static CustomLevelData g_level;
static ServerState g_sim;

/* ---- the district's own staff record ------------------------------------------------------------ */

static char *read_text(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); return NULL; }
    buf[n] = '\0';
    *out_len = (size_t)n;
    return buf;
}

/* civic_staff -- the twelve (x,y,z) the district tool wrote under "municipal_city": { ..., "characters":
 * [...] }. Read from the same file the engine loads, so this is the TOOL's own record of who it staffed,
 * not a guess at which characters look civic. */
static int civic_staff(float *xs, float *ys, float *zs, int *roles, int max) {
    size_t len = 0;
    char *buf = read_text(CITY_PATH, &len);
    if (!buf) return -1;
    const char *end = buf + len;
    int n = 0;
    const char *mc = level_boxes_find_key(buf, end, "municipal_city");
    const char *ch = mc ? level_boxes_find_key(mc, end, "characters") : NULL;
    if (ch) {
        const char *arr = level_boxes_skip_ws(ch);
        const char *arr_end = (*arr == '[') ? level_boxes_find_array_end(arr, end) : NULL;
        const char *c = arr_end ? arr + 1 : NULL;
        while (c && c < arr_end && n < max) {
            c = level_boxes_skip_ws(c);
            if (c >= arr_end) break;
            if (*c == ',') { c++; continue; }
            if (*c != '{') { c++; continue; }
            const char *obj_end = strchr(c, '}');
            if (!obj_end || obj_end > arr_end) break;
            const char *v;
            xs[n] = ys[n] = zs[n] = 0.0f;
            roles[n] = -1;
            if ((v = level_boxes_find_key(c, obj_end, "x"))) level_boxes_parse_number(v, &xs[n]);
            if ((v = level_boxes_find_key(c, obj_end, "y"))) level_boxes_parse_number(v, &ys[n]);
            if ((v = level_boxes_find_key(c, obj_end, "z"))) level_boxes_parse_number(v, &zs[n]);
            if ((v = level_boxes_find_key(c, obj_end, "role"))) {
                float role = -1.0f;
                if (level_boxes_parse_number(v, &role)) roles[n] = (int)role;
            }
            n++;
            c = obj_end + 1;
        }
    }
    free(buf);
    return n;
}

/* A placement's own "level" string, e.g. "mun_courthouse". The characters record is written two bodies
   per placement in placements order, so body i belongs to building i/2 -- which is how the civic boss's
   room is checked without duplicating the tool's geometry here. */
static int json_string_after(const char *key_value, char *out, size_t cap) {
    const char *q = strchr(key_value, '"');
    if (!q) return 0;
    const char *s = q + 1;
    const char *e = strchr(s, '"');
    if (!e) return 0;
    size_t n = (size_t)(e - s);
    if (n >= cap) n = cap - 1;
    memcpy(out, s, n);
    out[n] = '\0';
    return 1;
}

static int civic_building_names(char names[][24], int max) {
    size_t len = 0;
    char *buf = read_text(CITY_PATH, &len);
    if (!buf) return -1;
    const char *end = buf + len;
    const char *mc = level_boxes_find_key(buf, end, "municipal_city");
    const char *bl = mc ? level_boxes_find_key(mc, end, "buildings") : NULL;
    int n = 0;
    if (bl) {
        const char *arr = level_boxes_skip_ws(bl);
        const char *arr_end = (*arr == '[') ? level_boxes_find_array_end(arr, end) : NULL;
        const char *c = arr_end ? arr + 1 : NULL;
        while (c && c < arr_end && n < max) {
            c = level_boxes_skip_ws(c);
            if (c >= arr_end) break;
            if (*c != '{') { c++; continue; }
            const char *obj_end = strchr(c, '}');
            if (!obj_end || obj_end > arr_end) break;
            const char *v = level_boxes_find_key(c, obj_end, "level");
            names[n][0] = '\0';
            if (v) json_string_after(v, names[n], 24);
            n++;
            c = obj_end + 1;
        }
    }
    free(buf);
    return n;
}

static int level_character_at(float x, float y, float z) {
    for (int i = 0; i < g_level.character_count; i++) {
        const LevelCharacter *c = &g_level.characters[i];
        if (fabsf(c->x - x) < 0.02f && fabsf(c->y - y) < 0.02f && fabsf(c->z - z) < 0.02f) return i;
    }
    return -1;
}

/* ---- the sim ------------------------------------------------------------------------------------- */

/* The brain writes intent; something has to integrate in_fwd. This goes through the engine's OWN basis
 * (shankpit_yaw_basis_from_deg -- forward is (-sin yaw, -cos yaw), the convention story_ai.c steers in)
 * rather than the (+sin, +cos) shorthand some earlier walk harnesses used: with that shorthand a body
 * walking "forward" travels backwards along its own yaw, which reads as closing distance if you only
 * measure displacement. Measured, not assumed: the first run of this file had every body's gap GROW from
 * 40.0 to 51.9 units and a pass/fail on direction is the whole point of the scenario. */
static void walk_step(ServerState *s, float dt) {
    for (int i = 1; i < MAX_CLIENTS; i++) {
        PlayerState *p = &s->players[i];
        if (!p->active || p->state == STATE_DEAD) continue;
        YawBasis b = shankpit_yaw_basis_from_deg(p->yaw);
        p->x += b.fwd_x * p->in_fwd * 20.0f * dt;
        p->z += b.fwd_z * p->in_fwd * 20.0f * dt;
    }
}

/* The host's own weapon tick. local_game.h's update_entity does exactly this call for every active
 * slot -- update_weapons(p, local_state.players, local_state.projectiles, p->in_shoot > 0, ...) -- so
 * running it per active slot here is the host's per-tick loop minus the movement/collision wrapper
 * (which damage does not need and which drags the whole client in). That is the whole point of this
 * section: the clerk's in_shoot is not read as "fired", it is handed to the same function the
 * player's trigger reaches. */
static Projectile g_proj[MAX_PROJECTILES];

static void weapon_tick(ServerState *s, unsigned int now_ms) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        PlayerState *p = &s->players[i];
        if (!p->active || p->state == STATE_DEAD) continue;
        update_weapons(p, s->players, g_proj, p->in_shoot > 0, p->in_reload > 0,
                       p->in_ability > 0, p->in_ability2 > 0, now_ms, 3000u);
    }
}

/* story_ai.c's own yaw convention: forward = (-sin yaw, -cos yaw). */
static float bearing_deg(float dx, float dz) {
    return atan2f(-dx, -dz) * 57.29578f;
}

/* Named around physics.h's own `angle_diff`, which returns a SIGNED delta; this one is the absolute
   facing error the assertions are written against. */
static float face_error_deg(float a, float b) {
    float d = fmodf(a - b + 540.0f, 360.0f) - 180.0f;
    return fabsf(d);
}

/* spawns the whole city, exactly as a level load does, and returns the slot the given authored
   character landed in. Every call starts from a clean brain table, so the body under test is measured
   with the other 68 already occupying their slots -- the budget is real, not assumed. */
static int spawn_city(int character_index) {
    memset(&g_sim, 0, sizeof g_sim);
    g_sim.scene_id = SCENE_VOXWORLD;
    g_sim.game_mode = MODE_ZOMBIES;
    g_sim.story_phase = STORY_PHASE_PLAYING;
    g_sim.players[0].active = 1;
    g_sim.players[0].state = STATE_ALIVE;
    g_sim.players[0].health = 100;
    g_sim.players[0].y = 8.0f;
    story_ai_reset(&g_sim);

    int wanted = -1;
    for (int i = 0; i < g_level.character_count; i++) {
        const LevelCharacter *lc = &g_level.characters[i];
        int id = story_ai_spawn_unit_role(&g_sim, lc->archetype, lc->role, lc->kit, lc->faction,
                                          lc->orders, lc->x, lc->y, lc->z);
        if (i == character_index) wanted = id;
    }
    return wanted;
}

int main(void) {
    printf("== the district, through the engine's own parser ==\n");
    ok(level_boxes_load_from_file(CITY_PATH, &g_level), "the city loads from the exported level");
    ok(g_level.character_count == MAX_CLIENTS - 1,
       "all 69 authored characters are in the file the engine reads");

    float xs[32], ys[32], zs[32];
    int roles[32];
    int ncivic = civic_staff(xs, ys, zs, roles, 32);
    printf("      %d civic bodies in the district's own record\n", ncivic);
    ok(ncivic == 12, "the district staffs two bodies in each of its six buildings");
    ok(g_level.character_count - ncivic == 57,
       "leaving the lofts their own 57 of the city's 69 simultaneous slots");

    int civic_char[32];
    int matched = 0;
    for (int i = 0; i < ncivic; i++) {
        civic_char[i] = level_character_at(xs[i], ys[i], zs[i]);
        if (civic_char[i] >= 0) matched++;
    }
    ok(matched == ncivic,
       "every body in that record is really in the array the engine loaded (the two cannot drift)");

    /* Six buildings, six jobs. Checked on the LOADED array, so a tool whose record disagrees with the
       array fails here too. */
    {
        int distinct_jobs = 0, engagers = 0, hostile = 0;
        for (int i = 0; i < ncivic; i++) {
            if (civic_char[i] < 0) continue;
            const LevelCharacter *c = &g_level.characters[civic_char[i]];
            int seen = 0;
            for (int j = 0; j < i; j++) {
                if (civic_char[j] < 0) continue;
                const LevelCharacter *o = &g_level.characters[civic_char[j]];
                if (o->archetype == c->archetype && o->role == c->role && o->kit == c->kit) seen = 1;
            }
            if (!seen) distinct_jobs++;
            if (c->orders & EU_ORDER_ENGAGE) engagers++;
            if (c->faction == EU_FACTION_HOSTILE) hostile++;
            printf("      job %d: %-7s archetype %d, brain role %d, kit %d, orders 0x%02x\n",
                   i / 2, eu_archetype_name(c->archetype), c->archetype, c->role, c->kit, c->orders);
        }
        ok(distinct_jobs == 7,
           "seven distinct jobs across six buildings -- the courthouse's pair is a bailiff and the boss");
        ok(engagers == ncivic, "every one of those jobs accepts ENGAGE");
        ok(hostile == ncivic, "and every one of them is authored hostile (the district's own side)");
    }

    printf("== a clerk notices a player in its own building ==\n");
    int fired = 0, turned = 0, ordered = 0, spawned = 0;
    for (int i = 0; i < ncivic; i++) {
        if (civic_char[i] < 0) continue;
        int slot = spawn_city(civic_char[i]);
        if (slot < 0) continue;
        spawned++;
        PlayerState *body = &g_sim.players[slot];

        /* The player walks in through the colonnade: six metres dead ahead of the body, where the
           district's own staffing put it. story_ai_spawn_enemy faces every body down +Z (yaw 180), so
           "ahead" is the body's own forward, not a fixed axis -- the same test works on a rotated range. */
        float r = body->yaw * 0.0174533f;
        float fx = -sinf(r), fz = -cosf(r);
        PlayerState *hero = &g_sim.players[0];
        hero->x = body->x + fx * 6.0f;
        hero->z = body->z + fz * 6.0f;
        hero->y = body->y;
        hero->state = STATE_ALIVE;
        hero->health = 100;

        int shots = 0;
        float closest_face = 999.0f;
        unsigned int t = 1000;
        for (int k = 0; k < 60; k++, t += 16) {
            story_ai_tick(&g_sim, t);
            if (body->in_shoot) shots++;
            float d = face_error_deg(body->yaw, bearing_deg(hero->x - body->x, hero->z - body->z));
            if (d < closest_face) closest_face = d;
            /* pin the player: this measures the body's response, not a chase */
            hero->x = body->x + fx * 6.0f;
            hero->z = body->z + fz * 6.0f;
        }
        if (story_ai_unit_order(slot) == EU_ORDER_ENGAGE) ordered++;
        if (shots > 0) fired++;
        if (closest_face < 20.0f) turned++;
        printf("      body %2d at (%.0f,%.0f) y=%.1f: %2d/60 ticks on the trigger, faced it within %.1f deg, order=%s\n",
               i, (double)body->x, (double)body->z, (double)body->y, shots, (double)closest_face,
               story_ai_unit_order(slot) == EU_ORDER_ENGAGE ? "ENGAGE" : "other");
    }
    ok(spawned == ncivic, "every civic body gets a real player slot (none is silently dropped)");
    ok(ordered == ncivic, "all twelve take the EMPIRE ENGAGE order with a player in front of them");
    ok(turned == ncivic, "and all twelve turn to face that player");
    ok(fired == ncivic, "and all twelve pull the trigger -- the clerks fight, not just stand there");

    printf("== a clerk acts on a player who backs off ==\n");
    {
        /* Not every brain walks forward, and that is the point of staffing six jobs. A storm caller
           holds 200 units, so a player at 40 makes it BACK OFF; a knife orderly closes. What every
           one of them has to do is ACT (close or take up its own range), and the melee brains have to
           close -- that is the whole of their behaviour. The earlier version of this section asserted
           "all twelve advance", which was true of twelve identical pursuers and is a lie about a
           district with a sniper in it. */
        int responded = 0, melee = 0, melee_closed = 0;
        for (int i = 0; i < ncivic; i++) {
            if (civic_char[i] < 0) continue;
            int slot = spawn_city(civic_char[i]);
            if (slot < 0) continue;
            PlayerState *body = &g_sim.players[slot];
            const LevelCharacter *c = &g_level.characters[civic_char[i]];
            float r = body->yaw * 0.0174533f;
            float fx = -sinf(r), fz = -cosf(r);
            PlayerState *hero = &g_sim.players[0];
            hero->x = body->x + fx * 40.0f;
            hero->z = body->z + fz * 40.0f;
            hero->y = body->y;
            hero->state = STATE_ALIVE;
            hero->health = 100;
            float d0 = sqrtf((hero->x - body->x) * (hero->x - body->x) + (hero->z - body->z) * (hero->z - body->z));
            unsigned int t = 1000;
            for (int k = 0; k < 60; k++, t += 16) { story_ai_tick(&g_sim, t); walk_step(&g_sim, 0.016f); }
            float d1 = sqrtf((hero->x - body->x) * (hero->x - body->x) + (hero->z - body->z) * (hero->z - body->z));
            printf("      body %2d role %-2d %.1f -> %.1f units in one second (%s)\n", i, c->role,
                   (double)d0, (double)d1,
                   d1 < d0 - 0.5f ? "closed" : (d1 > d0 + 0.5f ? "backed off to its range" : "held"));
            if (fabsf(d1 - d0) > 0.5f) responded++;
            if (c->role == AI_ROLE_RELENTLESS_PURSUER) { melee++; if (d1 < d0 - 0.5f) melee_closed++; }
        }
        ok(responded == ncivic, "all twelve act on a player who backs off (close, or take up their range)");
        ok(melee > 0 && melee_closed == melee, "the knife orderlies close -- that is their entire brain");
    }

    printf("== the civic boss: a magistrate, not a twelfth clerk ==\n");
    {
        int boss = -1, boss_count = 0;
        for (int i = 0; i < ncivic; i++) {
            if (roles[i] != AI_ROLE_MAGISTRATE) continue;
            boss = i;
            boss_count++;
        }
        char bldg[32][24];
        int nbuild = civic_building_names(bldg, 32);
        ok(boss_count == 1, "the district authorises exactly one magistrate (AI_ROLE_MAGISTRATE)");
        ok(boss >= 0 && nbuild == 6 && strcmp(bldg[boss / 2], "mun_courthouse") == 0,
           "and it stands in the courthouse, behind the bailiff, not in a seventh building");

        /* One magnum in the district. Every clerk carries what its own brain picked -- rifle, shotgun,
           knife, sniper -- so the boss's weapon is its most visible property and the cheapest to check. */
        int magnums = 0;
        for (int i = 0; i < ncivic; i++) {
            if (civic_char[i] < 0) continue;
            int s = spawn_city(civic_char[i]);
            if (s < 0) continue;
            if (g_sim.players[s].current_weapon == WPN_MAGNUM) magnums++;
        }
        ok(magnums == 1, "the bench carries WPN_MAGNUM -- the only magnum in the district");

        int slot = (boss >= 0 && civic_char[boss] >= 0) ? spawn_city(civic_char[boss]) : -1;
        if (slot < 0) {
            ok(0, "the magistrate spawns into a real slot like every other body");
        } else {
            PlayerState *body = &g_sim.players[slot];
            PlayerState *hero = &g_sim.players[0];
            float r = body->yaw * 0.0174533f;
            float fx = -sinf(r), fz = -cosf(r);
            unsigned int t = 1000;

            /* 40 units apart, one second, and the player stays put: a pursuer closes, the bench holds. */
            hero->x = body->x + fx * 40.0f;
            hero->z = body->z + fz * 40.0f;
            hero->y = body->y;
            hero->state = STATE_ALIVE;
            hero->health = 100;
            float d0 = sqrtf((hero->x - body->x) * (hero->x - body->x) +
                             (hero->z - body->z) * (hero->z - body->z));
            for (int k = 0; k < 60; k++, t += 16) { story_ai_tick(&g_sim, t); walk_step(&g_sim, 0.016f); }
            float d1 = sqrtf((hero->x - body->x) * (hero->x - body->x) +
                             (hero->z - body->z) * (hero->z - body->z));
            printf("      magistrate at %.0f units: %.1f -> %.1f in one second\n", (double)d0,
                   (double)d0, (double)d1);
            ok(d1 >= d0 - 0.5f,
               "and holds its range where the knife orderlies close -- the bench does not charge");

            /* In sight and in range: the verdict holds the player in contempt. That is a write to the
               real stunned_until_ms field -- the same one local_game.h's own update_entity gates input
               on (`cmd_time < p->stunned_until_ms`) -- so it is this role's ability, not a new system. */
            slot = spawn_city(civic_char[boss]);
            body = &g_sim.players[slot];
            hero = &g_sim.players[0];
            r = body->yaw * 0.0174533f;
            fx = -sinf(r);
            fz = -cosf(r);
            hero->x = body->x + fx * 20.0f;
            hero->z = body->z + fz * 20.0f;
            hero->y = body->y;
            hero->state = STATE_ALIVE;
            hero->health = 100;
            hero->stunned_until_ms = 0;
            unsigned int peak = 0;
            t = 1000;
            for (int k = 0; k < 240; k++, t += 16) {
                story_ai_tick(&g_sim, t);
                if (hero->stunned_until_ms > peak) peak = hero->stunned_until_ms;
            }
            printf("      the verdict: hero held until tick %u (the tick clock reached %u)\n", peak, t);
            ok(peak > 1000u,
               "with the player in sight it holds them in contempt (a real stun deadline, not a flag)");

            /* Out of sight there is nothing to hold in contempt -- the ability is not a proximity aura. */
            slot = spawn_city(civic_char[boss]);
            body = &g_sim.players[slot];
            hero = &g_sim.players[0];
            r = body->yaw * 0.0174533f;
            fx = -sinf(r);
            fz = -cosf(r);
            hero->x = body->x + fx * 400.0f;
            hero->z = body->z + fz * 400.0f;
            hero->y = body->y;
            hero->state = STATE_ALIVE;
            hero->health = 100;
            hero->stunned_until_ms = 0;
            t = 1000;
            for (int k = 0; k < 240; k++, t += 16) story_ai_tick(&g_sim, t);
            ok(hero->stunned_until_ms == 0, "and it does not sentence a player it cannot see");

            /* The verdict is telegraphed (founder: "telegraph verdict"). The sentence is read, then
               served: the AI arms a real tell window and only stuns when that window elapses with
               the player still in sight. Both halves are observable through
               story_ai_verdict_tell_fill -- the same accessor apps/lobby's world ring and boss bar
               draw from, so this is the state the player is shown, not a second copy of the rule. */
            slot = spawn_city(civic_char[boss]);
            body = &g_sim.players[slot];
            hero = &g_sim.players[0];
            r = body->yaw * 0.0174533f;
            fx = -sinf(r);
            fz = -cosf(r);
            hero->x = body->x + fx * 20.0f;
            hero->z = body->z + fz * 20.0f;
            hero->y = body->y;
            hero->state = STATE_ALIVE;
            hero->health = 100;
            hero->stunned_until_ms = 0;
            unsigned int ev_tells0 = 0, ev_served0 = 0;
            story_ai_magistrate_event_counts(&ev_tells0, &ev_served0);
            unsigned int tell_start = 0, stun_at = 0;
            int stun_before_tell = 0;
            float max_fill = -1.0f;
            t = 1000;
            for (int k = 0; k < 240; k++, t += 16) {
                story_ai_tick(&g_sim, t);
                float f = story_ai_verdict_tell_fill(slot, t);
                if (f >= 0.0f) {
                    if (!tell_start) tell_start = t;
                    if (f > max_fill) max_fill = f;
                }
                if (!stun_at && hero->stunned_until_ms > 0) {
                    stun_at = t;
                    if (!tell_start) stun_before_tell = 1;
                }
            }
            printf("      the tell: sentence read from tick %u, served at tick %u (fill peaked at %.2f)\n",
                   tell_start, stun_at, (double)max_fill);
            ok(tell_start != 0,
               "the verdict is announced before it lands -- a real tell window, not an instant hit");
            ok(!stun_before_tell, "and no sentence is served before that window is up");
            ok(stun_at >= tell_start + 600u,
               "the bench reads the sentence for 600 ms first -- enough time to break the line");
            ok(max_fill > 0.5f && max_fill <= 1.0f,
               "the tell's own fill reaches the client -- the value the ring and the boss bar draw from");
            ok(story_ai_verdict_tell_fill(slot, t) < 0.0f,
               "and it clears once the sentence has been served");

            /* The presentation layer (spatial gavel cue, embers, shader ring) is EDGE-triggered off
               these counters: one event per read, one per sentence, so a client that polls them plays
               each sound and burst exactly once rather than re-firing while the fill value moves. */
            unsigned int ev_tells1 = 0, ev_served1 = 0;
            story_ai_magistrate_event_counts(&ev_tells1, &ev_served1);
            ok(ev_tells1 == ev_tells0 + 1u && ev_served1 == ev_served0 + 1u,
               "and the cue gets exactly one read event and one served event -- edge-triggered, not polled");

            /* Breaking the line mid-sentence drops it: the window is real, not decoration painted
               over a hit that was already guaranteed. */
            slot = spawn_city(civic_char[boss]);
            body = &g_sim.players[slot];
            hero = &g_sim.players[0];
            r = body->yaw * 0.0174533f;
            fx = -sinf(r);
            fz = -cosf(r);
            hero->x = body->x + fx * 20.0f;
            hero->z = body->z + fz * 20.0f;
            hero->y = body->y;
            hero->state = STATE_ALIVE;
            hero->health = 100;
            hero->stunned_until_ms = 0;
            unsigned int ct0 = 0, cs0 = 0, ct1 = 0, cs1 = 0;
            story_ai_magistrate_event_counts(&ct0, &cs0);
            int tell_seen = 0;
            t = 1000;
            for (int k = 0; k < 120 && !tell_seen; k++, t += 16) {
                story_ai_tick(&g_sim, t);
                if (story_ai_verdict_tell_fill(slot, t) >= 0.0f) tell_seen = 1;
            }
            hero->x = body->x + fx * 400.0f;
            hero->z = body->z + fz * 400.0f;
            hero->stunned_until_ms = 0;
            for (int k = 0; k < 60; k++, t += 16) story_ai_tick(&g_sim, t);
            printf("      breaking the line: tell seen %d, fill after %.2f, hero stun %u\n", tell_seen,
                   (double)story_ai_verdict_tell_fill(slot, t), hero->stunned_until_ms);
            story_ai_magistrate_event_counts(&ct1, &cs1);
            ok(tell_seen && story_ai_verdict_tell_fill(slot, t) < 0.0f && hero->stunned_until_ms == 0,
               "and breaking the line mid-sentence cancels it -- the tell is a real window");
            ok(ct1 > ct0 && cs1 == cs0,
               "a dropped sentence counts as read but never served -- the cue and the burst fire once, for the right event");
            /* The scorch is a HAZARD, not a decal (founder: "give scorches a hazard"). The pool is the
               simulation's own (story_ai_magistrate_scorches), so this reads the sim's view rather than a
               renderer's copy: a served sentence leaves a mark covering the floor the player stands on, and
               standing in it costs health. The bench is then killed on purpose -- the mark outlives it, and
               with the magnum silent the burn is the only thing taking health off the player. */
            slot = spawn_city(civic_char[boss]);
            body = &g_sim.players[slot];
            hero = &g_sim.players[0];
            r = body->yaw * 0.0174533f;
            fx = -sinf(r);
            fz = -cosf(r);
            hero->x = body->x + fx * 8.0f;    /* inside VERDICT_SCORCH_RADIUS */
            hero->z = body->z + fz * 8.0f;
            hero->y = body->y;
            hero->state = STATE_ALIVE;
            hero->health = 100;
            hero->stunned_until_ms = 0;
            hero->scene_id = body->scene_id;   /* the hazard is scene-bound, and so is the body */
            unsigned int served0 = 0, served1 = 0;
            story_ai_magistrate_event_counts(NULL, &served0);
            t = 1000;
            for (int k = 0; k < 240; k++, t += 16) {
                story_ai_tick(&g_sim, t);
                story_ai_magistrate_event_counts(NULL, &served1);
                if (served1 != served0) break;   /* the sentence is served: its mark is on the floor now */
            }
            const VerdictScorches *scorches = story_ai_magistrate_scorches();
            body->state = STATE_DEAD;          /* the mark outlives the bench; nothing else can shoot now */
            int covered = 0;
            for (int i = 0; i < VERDICT_SCORCH_MAX; i++)
                if (verdict_scorch_covers(&scorches->pool[i], hero->scene_id, hero->x, hero->z, t)) covered++;
            hero->health = 100;                /* measure the burn alone */
            for (int k = 0; k < 60; k++, t += 16) story_ai_tick(&g_sim, t);
            int burned = 100 - hero->health;
            printf("      the scorch: %d live mark(s), covering the player %d, 100 -> %d hp in one second\n",
                   verdict_scorches_live(scorches), covered, hero->health);
            ok(verdict_scorches_live(scorches) >= 1 && covered >= 1,
               "a served sentence leaves a scorch on the floor the player is actually standing in");
            ok(burned >= VERDICT_SCORCH_DAMAGE * 2 && burned <= VERDICT_SCORCH_DAMAGE * 8,
               "and standing in it burns -- about 8 hp a second, not a decorative decal");
            /* LINE OF SIGHT (founder: "also require line of site"). Same dead bench, same player standing
               in the same mark, same second -- only the geometry changes. Without this, "cover" in the
               gallery is decoration: the fire would reach through a bench because the distance was small. */
            {
                SightBox wall;
                wall.x = body->x + fx * 4.0f;
                wall.y = body->y + 1.5f;
                wall.z = body->z + fz * 4.0f;
                wall.w = 2.0f; wall.h = 4.0f; wall.d = 2.0f; wall.solid = 1;
                story_ai_set_sight(&wall, 1);
                hero->x = body->x + fx * 8.0f;
                hero->z = body->z + fz * 8.0f;
                hero->health = 100;
                for (int k = 0; k < 60; k++, t += 16) story_ai_tick(&g_sim, t);
                printf("      cover: a wall between the mark and the player -> %d hp\n", hero->health);
                ok(hero->health == 100,
                   "a wall between the mark and the player stops the burn -- cover is a rule, not decoration");
                story_ai_set_sight(NULL, 0);

                /* And the same wall stops the bench SEEING at all: the sentence is not read through it
                   (perception's new sight test), not merely not served. */
                story_ai_reset(&g_sim);
                slot = spawn_city(civic_char[boss]);
                body = &g_sim.players[slot];
                hero = &g_sim.players[0];
                r = body->yaw * 0.0174533f;
                fx = -sinf(r); fz = -cosf(r);
                hero->x = body->x + fx * 20.0f;
                hero->z = body->z + fz * 20.0f;
                hero->y = body->y;
                hero->state = STATE_ALIVE;
                hero->health = 100;
                hero->stunned_until_ms = 0;
                wall.x = body->x + fx * 10.0f;
                wall.z = body->z + fz * 10.0f;
                story_ai_set_sight(&wall, 1);
                for (int k = 0; k < 60; k++, t += 16) story_ai_tick(&g_sim, t);
                printf("      cover: the tell through a wall reads %.2f, hero stun %u\n",
                       (double)story_ai_verdict_tell_fill(slot, t), hero->stunned_until_ms);
                ok(story_ai_verdict_tell_fill(slot, t) < 0.0f && hero->stunned_until_ms == 0,
                   "and a wall stops the sentence being read at all -- sight is required, not just range");
                story_ai_set_sight(NULL, 0);
            }
            hero->x = body->x + fx * 200.0f;   /* out of the mark, same dead bench, same second */
            hero->z = body->z + fz * 200.0f;
            hero->health = 100;
            for (int k = 0; k < 60; k++, t += 16) story_ai_tick(&g_sim, t);
            ok(hero->health == 100,
               "stepping out of the mark stops it -- the hazard is a place, not a status on the player");
        }
    }

    printf("== the shot lands: the host's own weapon tick, and the player's health ==\n");
    {
        int hurt = 0;
        for (int i = 0; i < ncivic; i++) {
            if (civic_char[i] < 0) continue;
            int slot = spawn_city(civic_char[i]);
            if (slot < 0) continue;
            PlayerState *body = &g_sim.players[slot];
            const LevelCharacter *c = &g_level.characters[civic_char[i]];
            float r = body->yaw * 0.0174533f;
            float fx = -sinf(r), fz = -cosf(r);
            PlayerState *hero = &g_sim.players[0];
            /* Six units ahead, same scene: the shot has to be able to reach, and the weapon pipeline
               skips targets in another scene entirely. */
            hero->x = body->x + fx * 6.0f;
            hero->z = body->z + fz * 6.0f;
            hero->y = body->y;
            hero->scene_id = g_sim.scene_id;
            hero->state = STATE_ALIVE;
            hero->health = 100;
            hero->shield = 0;
            int before = hero->health;
            unsigned int t = 1000;
            for (int k = 0; k < 120 && hero->health > 0; k++, t += 16) {
                story_ai_tick(&g_sim, t);   /* the brain writes in_shoot */
                weapon_tick(&g_sim, t);     /* the host turns it into a shot, and a shot into damage */
            }
            if (hero->health < before) hurt++;
            printf("      body %2d role %-2d weapon %-2d: hero health %d -> %d\n",
                   i, c->role, (int)body->current_weapon, before, hero->health);
        }
        ok(hurt == ncivic,
           "all twelve take health off the player through the real weapon pipeline (not just in_shoot)");
    }

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
