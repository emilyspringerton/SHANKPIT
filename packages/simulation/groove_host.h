/* groove_host.h -- groove combat inside the engine: the knife becomes a dance-fighting weapon, its E is the beat sync
 * device, the AR plays to the synced beat, and every on-beat move or gun hit climbs the style ranks D..SSS.
 *
 * Founder 2026-11-18: "bring it all into the construct and zombie sandbox environments with real player facing
 * affordances", "a bpm sync device as the special for the shank ... you have to switch to 1 for knife sync the beat
 * and then back to 3 to use it".
 *
 * The player-facing controls, knife out (key 1):
 *   left mouse            JAB; with W a SNAP KICK; crouched a SWEEP; in the air a jumping kick
 *   right mouse           SPIN; with W a LEAN; with S a GLIDE (the evade)
 *   both                  FREEZE, once the style meter has 600
 *   E                     SYNC: tap it on the beat; four steady taps lock the tempo for 32 beats
 * AR out (key 3), while synced: it fires on the beat and hits harder. Ranks pay out on every weapon.
 *
 * Header-only on purpose: physics.h's hooks are `static`, one copy per translation unit, so this has to be compiled
 * into the same unit as update_weapons (apps/lobby/src/main.c, the dedicated server) -- the same reason
 * stats_apply.h is a header. Install AFTER stats_apply_install: the ROF hook here chains to whatever was there.
 * The rules are PARENA (groove_combat_rules.c, groove_sync_rules.c); the fighters live in groove_combat.h /
 * groove_sync.h; every event is REFLUX 1200-1299.
 */
#ifndef SHANKPIT_GROOVE_HOST_H
#define SHANKPIT_GROOVE_HOST_H

#include "groove_sync.h"

#define GROOVE_MS_PER_TICK 16
#define GROOVE_MAX_HEALTH 100

typedef int (*GrooveBeatPhaseFn)(unsigned int now_ms);   /* 0..999 within the playing beat (a MIXFORGE deck) */

static struct {
    int enabled;
    PlayerState *players;          /* the array update_weapons is given as `targets` */
    GrooveFighter f[MAX_CLIENTS];
    GrooveSync s[MAX_CLIENTS];
    int prev_buttons[MAX_CLIENTS];
    PhysPlayerStatsHook prev_rof;  /* chained: a GFD job's ROF stays in force */
    GrooveBeatPhaseFn beat_phase;  /* NULL: groove_fallback_phase, a steady 120 BPM */
} g_groove;

static inline int groove_host_id(const PlayerState *p) {
    int id;
    if (!g_groove.players || !p) return -1;
    id = (int)(p - g_groove.players);
    return (id >= 0 && id < MAX_CLIENTS) ? id : -1;
}

/* The beat a player is graded against: their own synced beat when the device holds a lock (you synced to what you
 * hear), else the music's (the deck), else the steady fallback. */
static inline int groove_host_phase(int id, unsigned int now_ms) {
    if (id >= 0 && g_groove.s[id].locked) return groove_sync_phase(&g_groove.s[id], (int)now_ms);
    return g_groove.beat_phase ? g_groove.beat_phase(now_ms) : groove_fallback_phase(now_ms);
}

static inline void groove_host_heal(PlayerState *p, int rank) {
    int heal = gs_rank_heal_per_hit(rank);
    if (heal > 0 && p->health > 0) {
        p->health += heal;
        if (p->health > GROOVE_MAX_HEALTH) p->health = GROOVE_MAX_HEALTH;
    }
}

/* The current move against everyone in reach this tick. */
static inline void groove_host_hits(PlayerState *p, PlayerState *targets, int id, unsigned int now_ms,
                                    unsigned int respawn_delay_ms) {
    GrooveFighter *f = &g_groove.f[id];
    float yaw_rad = -p->yaw * 0.0174533f;          /* physics.h: forward = (sin r, -cos r), r = -yaw in radians */
    int i, staggered = 0;
    for (i = 0; i < MAX_CLIENTS; i++) {
        PlayerState *t = &targets[i];
        float dy;
        int dmg;
        if (i == id || !t->active || t->state == STATE_DEAD) continue;
        if (t->scene_id != p->scene_id || phys_is_friendly(p, t)) continue;
        dy = t->y - p->y;
        if (dy > PLAYER_HEIGHT || dy < -PLAYER_HEIGHT) continue;
        if (groove_freeze_fires(f)) {
            float dx = t->x - p->x, dz = t->z - p->z;
            if (dx * dx + dz * dz <= groove_freeze_radius() * groove_freeze_radius()) {
                groove_stagger(&g_groove.f[i]);
                t->vx = 0.0f; t->vz = 0.0f;
                staggered++;
            }
            continue;
        }
        dmg = groove_hit(f, id, p->x, p->z, yaw_rad, i, t->x, t->z, &g_groove.f[i]);
        if (dmg > 0) {
            float kx, kz;
            groove_knockback(f, p->x, p->z, yaw_rad, t->x, t->z, &kx, &kz);
            dmg = dmg * gs_rank_damage_pct(f->rank) / 100;
            katana_apply_damage(p, t, dmg, f->grade == GC_GRADE_PERFECT ? 20 : 10, now_ms, respawn_delay_ms);
            combat_text_note_damage(p, dmg, now_ms);
            t->vx += kx; t->vz += kz;
            if (gc_knockdown(f->move)) t->vy += 0.35f;
            groove_host_heal(p, f->rank);
        }
    }
    if (groove_freeze_fires(f)) groove_freeze_report(id, staggered);
}

/* g_phys_groove_hook: every tick for every fighter; takes the knife over. */
static int groove_host_weapons_hook(PlayerState *p, PlayerState *targets, int weapon, int shoot, int ability_press,
                                    int ability2_press, unsigned int now_ms, unsigned int respawn_delay_ms) {
    int id, buttons, rise;
    GrooveFighter *f;
    if (!g_groove.enabled || !targets) return 0;
    if (!g_groove.players) g_groove.players = targets;
    id = groove_host_id(p);
    if (id < 0) return 0;
    f = &g_groove.f[id];
    groove_tick(f);
    groove_sync_update(&g_groove.s[id], id, (int)now_ms);
    groove_rank_update(f, id);
    if (f->move != GC_MOVE_NONE) groove_host_hits(p, targets, id, now_ms, respawn_delay_ms);
    if (weapon != WPN_KNIFE || p->state == STATE_DEAD) { g_groove.prev_buttons[id] = 0; return 0; }

    buttons = (shoot ? 1 : 0) | (ability2_press ? 2 : 0) | (ability_press ? 4 : 0);
    rise = buttons & ~g_groove.prev_buttons[id];
    g_groove.prev_buttons[id] = buttons;
    if (rise & 4) groove_sync_tap(&g_groove.s[id], id, (int)now_ms);
    if (rise & 3)
        groove_input(f, id, (buttons & 1) ? 1 : 0, (buttons & 2) ? 1 : 0, p->in_fwd > 0.3f, p->in_fwd < -0.3f,
                     p->crouching ? 1 : 0, p->on_ground ? 0 : 1, groove_host_phase(id, now_ms));
    if (f->move != GC_MOVE_NONE) { p->is_shooting = 3; p->recoil_anim = 0.4f; }
    return 1;   /* the knife is the groove's: no plain stab, no storm-charge fallback on E */
}

/* g_phys_weapon_hit_hook: a gun hit -- the synced AR's damage, the rank bonus, the style meter, SS+ healing. */
static int groove_host_weapon_hit_hook(PlayerState *p, PlayerState *target, int weapon, int damage, unsigned int now_ms) {
    int id = groove_host_id(p), out;
    (void)target;
    if (!g_groove.enabled || id < 0) return damage;
    out = groove_sync_damage(&g_groove.s[id], weapon, damage, g_groove.f[id].rank);
    groove_gun_hit(&g_groove.f[id], id, weapon, groove_host_phase(id, now_ms));
    groove_host_heal(p, g_groove.f[id].rank);
    return out;
}

/* The ROF hook: the synced AR's cooldown snapped to the beat, SSS fires faster; a job's own ROF applies first. */
static int groove_host_rof_hook(const PlayerState *p, int base_ticks) {
    int ticks = g_groove.prev_rof ? g_groove.prev_rof(p, base_ticks) : base_ticks;
    int id = groove_host_id(p), ms;
    if (!g_groove.enabled || id < 0) return ticks;
    ms = groove_sync_cooldown_ms(&g_groove.s[id], p->current_weapon, ticks * GROOVE_MS_PER_TICK, g_groove.f[id].rank);
    ticks = (ms + GROOVE_MS_PER_TICK / 2) / GROOVE_MS_PER_TICK;
    return ticks < 1 ? 1 : ticks;
}

/* Install once, after stats_apply_install (if any). `players` is the array update_weapons sees; `beat_phase` the
 * music's beat (NULL: a steady 120 BPM). Nothing changes until groove_host_set_enabled(1). */
static inline void groove_host_install(PlayerState *players, GrooveBeatPhaseFn beat_phase) {
    int i;
    memset(g_groove.f, 0, sizeof g_groove.f);
    for (i = 0; i < MAX_CLIENTS; i++) groove_sync_init(&g_groove.s[i]);
    memset(g_groove.prev_buttons, 0, sizeof g_groove.prev_buttons);
    g_groove.players = players;
    g_groove.beat_phase = beat_phase;
    if (g_phys_stats_rof_hook != groove_host_rof_hook) g_groove.prev_rof = g_phys_stats_rof_hook;
    phys_set_player_stats_hooks(g_phys_stats_health_hook, groove_host_rof_hook);
    phys_set_groove_hooks(groove_host_weapons_hook, groove_host_weapon_hit_hook);
}

/* On in the zombie sandbox and the construct, off everywhere else (call it each frame or on mode change). */
static inline void groove_host_set_enabled(int on) {
    int i;
    if (on && !g_groove.enabled) {
        for (i = 0; i < MAX_CLIENTS; i++) { groove_init(&g_groove.f[i]); groove_sync_init(&g_groove.s[i]); }
    }
    g_groove.enabled = on ? 1 : 0;
}

/* For the HUD. */
static inline const GrooveFighter *groove_host_fighter(int id) { return (id >= 0 && id < MAX_CLIENTS) ? &g_groove.f[id] : NULL; }
static inline const GrooveSync *groove_host_sync(int id) { return (id >= 0 && id < MAX_CLIENTS) ? &g_groove.s[id] : NULL; }
static inline int groove_host_enabled(void) { return g_groove.enabled; }

#endif
