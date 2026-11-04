/* packages/simulation/trapx.c -- the VS0 host. State, clock, bank, bus. No decision lives here that the .prn
 * could make: every threshold below is a call into trapx_rules.c. */

#include "trapx.h"

#include <string.h>

/* The generated rules, by their emitted names. Declared rather than included so this file reads as the host it
 * is -- see trapx_rules.c for the bodies, which are PARENA's. */
extern int trapx_scene_known(int scene);
extern int trapx_scene_fos(int scene);
extern int trapx_scene_watcher_density(int scene);
extern int trapx_portal_target(int scene);
extern int trapx_alertness_step(int alertness, int density, int heat, int quiet);
extern int trapx_trust_step(int trust, int clean, int dirty);
extern int trapx_enforcement_level(int alertness, int trust, int heat);
extern int trapx_heat_step(int heat, int action, int enforcement);
extern int trapx_claim_cost(int scene, int owned);
extern int trapx_flow_per_tick(int integrity, int enforcement);
extern int trapx_pressure_per_tick(int attention, int enforcement, int fos);
extern int trapx_attention_step(int attention, int k9);
extern int trapx_k9_worth(int deployed);
extern int trapx_integrity_decay(int integrity, int attention, int k9);
extern int trapx_rogue_swarm(int integrity, int attention, int pressure);
extern int trapx_receipt_known(int kind);
extern int trapx_receipt_digest(int kind, int amount, int tick);
extern int trapx_public_kill_heat(int witnesses);
extern int trapx_public_kill_trust(int witnesses);

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int trapx_city_index(const TrapxCity *city, int scene) {
    if (!city) return -1;
    for (int i = 0; i < TRAPX_VS0_SCENES; i++) {
        if (city->districts[i].scene == scene) return i;
    }
    return -1;
}

TrapxDistrict *trapx_city_here(TrapxCity *city) {
    int i = city ? trapx_city_index(city, city->scene) : -1;
    return (i < 0) ? NULL : &city->districts[i];
}

static TrapxDistrict *district_of(TrapxCity *city, int scene) {
    int i = trapx_city_index(city, scene);
    return (i < 0) ? NULL : &city->districts[i];
}

int trapx_city_enforcement(const TrapxCity *city, int scene) {
    int i = trapx_city_index(city, scene);
    return (i < 0) ? 0 : city->districts[i].enforcement;
}

/* The two VS0 scenes, written down once. A third scene added to the .prn without being added here is caught by
 * trapx_test's "the rules and the host agree about the scene list" check rather than silently existing. */
static const int VS0_SCENES[TRAPX_VS0_SCENES] = {TRAPX_SCENE_APARTMENT, TRAPX_SCENE_SCHOOL};

void trapx_city_register_default_mods(TrapxCity *city) {
    if (!city) return;
    TrapxMods *m = &city->mods;
    trapx_mods_reset(m);
    /* The three mods VS0 actually ships. Their names are the API: an out-of-tree mod registers its own and asks
     * for the kinds it wants; nothing else in this file changes when it does. */
    trapx_mods_add(m, "trapx.ledger", (1u << TRAPX_EV_RECEIPT), 100);
    trapx_mods_add(m, "trapx.watcher", (1u << TRAPX_EV_ALERT) | (1u << TRAPX_EV_ENFORCE), 50);
    trapx_mods_add(m, "trapx.k9", (1u << TRAPX_EV_K9) | (1u << TRAPX_EV_ROGUE), 20);
}

void trapx_city_init(TrapxCity *city, int scene) {
    if (!city) return;
    memset(city, 0, sizeof(*city));
    for (int i = 0; i < TRAPX_VS0_SCENES; i++) {
        TrapxDistrict *d = &city->districts[i];
        int s = VS0_SCENES[i];
        d->scene = s;
        d->alertness = 0;
        d->trust = 0;
        d->heat = 0;
        d->enforcement = 0;
        d->fos_total = trapx_scene_fos(s);
        d->fos_held = 0;
        d->attention = 0;
        d->integrity = 100;   /* an office nobody has touched is intact; with none held it stays there */
        d->pressure = 0;
        d->k9 = 0;
    }
    city->scene = trapx_scene_known(scene) ? scene : TRAPX_SCENE_APARTMENT;
    city->flow = TRAPX_START_FLOW;
    city->ledger = 0;
    trapx_events_reset(&city->events);
    trapx_city_register_default_mods(city);
}

static int band(int alertness) { return alertness / 25; }

int trapx_city_announce(TrapxCity *city, int kind, int scene, int a, int b, int *out, int out_cap) {
    if (!city) return 0;
    trapx_events_push(&city->events, kind, city->tick, scene, a, b);
    const TrapxEvent *ev = trapx_events_at(&city->events, 0);
    return trapx_mods_deliver(&city->mods, ev, out, out_cap);
}

int trapx_city_tick(TrapxCity *city, int quiet) {
    if (!city) return 0;
    city->tick += 1;
    int delivered[TRAPX_MOD_CAP];
    for (int i = 0; i < TRAPX_VS0_SCENES; i++) {
        TrapxDistrict *d = &city->districts[i];
        int density = trapx_scene_watcher_density(d->scene);
        int before_band = band(d->alertness);
        int before_level = d->enforcement;

        d->alertness = trapx_alertness_step(d->alertness, density, d->heat, quiet ? 1 : 0);
        d->heat = trapx_heat_step(d->heat, TRAPX_ACTION_NONE, d->enforcement);
        d->enforcement = trapx_enforcement_level(d->alertness, d->trust, d->heat);

        /* Pressure and attention are the office's weather. With nothing held there is no office to pressurise, so
         * the office dials stay where they are rather than pretending a phantom office exists. */
        if (d->fos_held > 0) {
            d->pressure = clamp_int(d->pressure + trapx_pressure_per_tick(d->attention, d->enforcement, d->fos_held) - 2, 0, 100);
            d->attention = trapx_attention_step(d->attention, d->k9);
            d->integrity = trapx_integrity_decay(d->integrity, d->attention, d->k9);
        }

        if (band(d->alertness) != before_band) {
            trapx_city_announce(city, TRAPX_EV_ALERT, d->scene, d->alertness, before_band * 25, delivered, TRAPX_MOD_CAP);
        }
        if (d->enforcement != before_level) {
            trapx_city_announce(city, TRAPX_EV_ENFORCE, d->scene, d->enforcement, before_level, delivered, TRAPX_MOD_CAP);
        }
        if (!d->rogue && d->fos_held > 0 && trapx_rogue_swarm(d->integrity, d->attention, d->pressure)) {
            d->rogue = 1;
            trapx_city_announce(city, TRAPX_EV_ROGUE, d->scene, d->integrity, d->attention, delivered, TRAPX_MOD_CAP);
            trapx_city_receipt(city, d->scene, TRAPX_RECEIPT_WITNESS, 0);
        }
    }
    int here = trapx_city_index(city, city->scene);
    return (here < 0) ? 0 : city->districts[here].enforcement;
}

int trapx_city_receipt(TrapxCity *city, int scene, int kind, int amount) {
    if (!city || !trapx_receipt_known(kind)) return -1;
    if (trapx_city_index(city, scene) < 0) return -1;
    int digest = trapx_receipt_digest(kind, amount, city->tick);
    city->ledger = (int)(((long)city->ledger + (long)digest) % TRAPX_LEDGER_MOD);
    if (city->ledger < 0) city->ledger += TRAPX_LEDGER_MOD;
    city->receipts += 1;
    TrapxDistrict *d = district_of(city, scene);
    if (kind == TRAPX_RECEIPT_K9 || kind == TRAPX_RECEIPT_WITNESS) d->dirty_receipts += 1;
    else d->clean_receipts += 1;
    int delivered[TRAPX_MOD_CAP];
    trapx_city_announce(city, TRAPX_EV_RECEIPT, scene, kind, amount, delivered, TRAPX_MOD_CAP);
    return digest;
}

int trapx_city_claim(TrapxCity *city, int scene) {
    if (!city) return TRAPX_ERR_ARG;
    if (!trapx_scene_known(scene)) return TRAPX_ERR_SCENE;
    if (city->scene != scene) return TRAPX_ERR_NOT_HERE;
    TrapxDistrict *d = district_of(city, scene);
    if (!d) return TRAPX_ERR_SCENE;
    if (d->fos_held >= d->fos_total) return TRAPX_ERR_FULL;
    int cost = trapx_claim_cost(scene, d->fos_held);
    if (city->flow < cost) return TRAPX_ERR_POOR;

    city->flow -= cost;
    d->fos_held += 1;
    if (d->fos_held == 1) {
        d->integrity = 100;
        d->attention = 0;
        d->pressure = 0;
    }
    int delivered[TRAPX_MOD_CAP];
    trapx_city_announce(city, TRAPX_EV_CLAIM, scene, scene, cost, delivered, TRAPX_MOD_CAP);
    trapx_city_receipt(city, scene, TRAPX_RECEIPT_CLAIM, cost);
    return TRAPX_OK;
}

int trapx_city_contest(TrapxCity *city, int scene) {
    if (!city) return TRAPX_ERR_ARG;
    if (!trapx_scene_known(scene)) return TRAPX_ERR_SCENE;
    if (city->scene != scene) return TRAPX_ERR_NOT_HERE;
    TrapxDistrict *d = district_of(city, scene);
    if (!d) return TRAPX_ERR_SCENE;
    if (d->fos_held <= 0) return TRAPX_ERR_NONE_HELD;

    d->pressure = clamp_int(d->pressure + 15, 0, 100);
    d->heat = trapx_heat_step(d->heat, TRAPX_ACTION_CONTEST, d->enforcement);
    int delivered[TRAPX_MOD_CAP];
    trapx_city_announce(city, TRAPX_EV_CONTEST, scene, scene, d->pressure, delivered, TRAPX_MOD_CAP);
    trapx_city_receipt(city, scene, TRAPX_RECEIPT_CONTEST, 0);
    return TRAPX_OK;
}

int trapx_city_deploy_k9(TrapxCity *city, int scene, int units) {
    if (!city) return TRAPX_ERR_ARG;
    if (!trapx_scene_known(scene)) return TRAPX_ERR_SCENE;
    if (city->scene != scene) return TRAPX_ERR_NOT_HERE;
    if (units < 0 || units > 5) return TRAPX_ERR_ARG;
    TrapxDistrict *d = district_of(city, scene);
    if (!d) return TRAPX_ERR_SCENE;

    d->k9 = units;
    d->heat = trapx_heat_step(d->heat, TRAPX_ACTION_DEPLOY_K9, d->enforcement);
    d->attention = trapx_attention_step(d->attention, units);
    int delivered[TRAPX_MOD_CAP];
    trapx_city_announce(city, TRAPX_EV_K9, scene, scene, units, delivered, TRAPX_MOD_CAP);
    trapx_city_receipt(city, scene, TRAPX_RECEIPT_K9, trapx_k9_worth(units));
    return TRAPX_OK;
}

int trapx_city_public_kill(TrapxCity *city, int scene, int witnesses) {
    if (!city) return TRAPX_ERR_ARG;
    if (!trapx_scene_known(scene)) return TRAPX_ERR_SCENE;
    if (witnesses < 0) return TRAPX_ERR_ARG;
    TrapxDistrict *d = district_of(city, scene);
    if (!d) return TRAPX_ERR_SCENE;

    d->heat = clamp_int(d->heat + trapx_public_kill_heat(witnesses), 0, 100);
    d->trust = clamp_int(d->trust + trapx_public_kill_trust(witnesses), -100, 100);
    d->kills += 1;
    d->enforcement = trapx_enforcement_level(d->alertness, d->trust, d->heat);
    int delivered[TRAPX_MOD_CAP];
    trapx_city_announce(city, TRAPX_EV_KILL, scene, witnesses, trapx_public_kill_heat(witnesses), delivered, TRAPX_MOD_CAP);
    trapx_city_receipt(city, scene, TRAPX_RECEIPT_WITNESS, witnesses);
    return TRAPX_OK;
}

int trapx_city_portal(TrapxCity *city, int to_scene) {
    if (!city) return TRAPX_ERR_ARG;
    if (!trapx_scene_known(to_scene)) return TRAPX_ERR_SCENE;
    if (trapx_portal_target(city->scene) != to_scene) return TRAPX_ERR_NO_PORTAL;
    int from = city->scene;
    city->scene = to_scene;
    int delivered[TRAPX_MOD_CAP];
    trapx_city_announce(city, TRAPX_EV_PORTAL, to_scene, from, to_scene, delivered, TRAPX_MOD_CAP);
    return TRAPX_OK;
}
