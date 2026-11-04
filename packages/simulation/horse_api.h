/* horse_api.h -- the public surface a MOD talks to horses through.
 *
 * Founder real-time: "actors should be goal oriented... all parena based; mod api focused; ensure
 * it all syncs to the network via reflux so other mods can interact". This header is the answer to
 * the middle two clauses, and it is deliberately small: five verbs and one poll. A mod does not
 * reach into HorseState to make something happen, and it does not need to know that a horse has a
 * mind (packages/simulation/humanness.h) behind its goal (horse_goals.h).
 *
 * THE INTERACTION MODEL IS REFLUX, NOT CALLBACKS (same reason reflux_runtime.h's own header gives:
 * VS0 has no closures yet). A mod that wants to know what horses are doing POLLS the horse family
 * of REFLUX actions with horse_api_poll()/HorseRefluxCursor, exactly the way zombies_hud_bridge.c
 * polls its own family -- it never registers, and the horse tick never knows it exists. A mod that
 * wants to change something CALLS one of the verbs below, each of which dispatches the same event
 * it caused, so other listeners see a mod-driven feed exactly like a player-driven one.
 *
 * NETWORK: the horse family is the first REFLUX family with a real wire bridge
 * (PACKET_HORSE_EVENTS + horse_sync.h). On a dedicated server the authoritative tick dispatches;
 * the bridge relays; the client re-dispatches into its own log, so a mod listening on the client
 * sees the same events. Horse STATE (where each horse is, what it is doing, who is on it) rides
 * PACKET_HORSE_STATE, because state is idempotent and events are not.
 *
 * This header is a façade over local_game.h's own horse section (it needs the tick's own
 * horse_goal_change/horse_spawn_defaults, and duplicating them here would be a second
 * implementation of the same rule). Include order: include this INSTEAD of local_game.h, not
 * before it.
 */
#ifndef HORSE_API_H
#define HORSE_API_H

#include "local_game.h"
#include "../reflux/reflux_runtime.h"

/* A mod's own read cursor into REFLUX. Zero-initialise it, then call horse_api_cursor_init once
 * (a fresh cursor must not replay the whole log's history at it). */
typedef struct {
    int cursor;
} HorseRefluxCursor;

typedef struct {
    int type;   /* REFLUX_ACTION_HORSE_* */
    int a, b, c;
} HorseEvent;

static inline void horse_api_cursor_init(HorseRefluxCursor *c) {
    if (c) c->cursor = reflux_host_log_size();
}

/* Every live horse in a scene, for a mod that wants to draw or reason about them. Pass scene_id < 0
 * to count every scene. */
static inline int horse_api_count(const ServerState *st, int scene_id) {
    if (!st) return 0;
    int n = 0;
    for (int i = 0; i < MAX_HORSES; i++)
        if (st->horses[i].active && (scene_id < 0 || st->horses[i].scene_id == scene_id)) n++;
    return n;
}

/* Read-only access to one horse. NULL when that slot is not a live horse. Deliberately returns the
 * state itself rather than a copy: a mod reading it every tick does not need an allocator, and the
 * verbs below are the only supported way to CHANGE one. */
static inline HorseState *horse_api_at(ServerState *st, int slot) {
    if (!st || slot < 0 || slot >= MAX_HORSES) return NULL;
    return st->horses[slot].active ? &st->horses[slot] : NULL;
}

/* Put a horse in the world (a farm, a mod's own paddock, a spawner). Returns the slot, or -1 when
 * every slot is taken. Dispatches REFLUX_ACTION_HORSE_SPAWNED through horse_spawn_defaults. */
static inline int horse_api_spawn(ServerState *st, int scene_id, float x, float z, float yaw) {
    if (!st) return -1;
    for (int i = 0; i < MAX_HORSES; i++) {
        if (st->horses[i].active) continue;
        horse_spawn_defaults(&st->horses[i], i, scene_id, x, z, yaw);
        return i;
    }
    return -1;
}

/* Ask a horse for a goal (HORSE_GOAL_*, protocol.h). This is how a mod "interacts" with an actor:
 * lead it home, send it grazing, make it bolt. The horse still decides HOW -- PARENA's duration,
 * speed and radius are the same ones its own tick would have used. Returns 1 when accepted. */
static inline int horse_api_set_goal(ServerState *st, int slot, int goal, unsigned int now_ms) {
    HorseState *h = horse_api_at(st, slot);
    if (!h || goal < 0 || goal >= HORSE_GOAL_COUNT) return 0;
    horse_goal_change(slot, h, goal, now_ms);
    return 1;
}

/* Feed a horse a carrot from the saddlebag (no rider required -- a mod can, a stable hand can).
 * Returns 1 when a carrot was actually eaten. Dispatches REFLUX_ACTION_HORSE_FED. */
static inline int horse_api_feed(ServerState *st, int slot, unsigned int now_ms) {
    HorseState *h = horse_api_at(st, slot);
    (void)now_ms;
    if (!h || h->carrots <= 0 || h->stamina_milli >= HORSE_STAMINA_MAX_MILLI) return 0;
    h->carrots--;
    h->stamina_milli += horse_rules()->carrot_restore_milli();
    if (h->stamina_milli > HORSE_STAMINA_MAX_MILLI) h->stamina_milli = HORSE_STAMINA_MAX_MILLI;
    horse_reflux(REFLUX_ACTION_HORSE_FED, slot, h->carrots, h->stamina_milli);
    return 1;
}

/* Drain the horse family of REFLUX actions dispatched since the last call. Returns how many rows
 * were written (0 when nothing new). Non-horse actions are skipped, not returned -- the family
 * boundary lives in reflux_action_is_horse, in one place.
 *
 * `max` is a batch cap, not a limit on what is delivered: the cursor is an ABSOLUTE dispatch
 * number and is left on the first row that did not fit, so a mod that wants everything calls this
 * until it returns 0 (a mod polling once per tick just calls it once). Deliberately absolute: the
 * retained log saturates at REFLUX_LOG_CAPACITY, so a high-water mark taken from the log's own size
 * goes permanently silent once the log has wrapped -- see reflux_runtime.h.
 *
 * That ring is the one honest bound: if more than REFLUX_LOG_CAPACITY (256) actions -- of ANY
 * family -- land before this is called, the oldest are overwritten and are not delivered. Polling
 * each tick (what this is for) makes that unreachable in practice; horse STATE is carried
 * separately and cannot be missed. */
static inline int horse_api_poll(HorseRefluxCursor *c, HorseEvent *out, int max) {
    if (!c || !out || max <= 0) return 0;
    int total = reflux_host_total_dispatched();
    if (c->cursor < 0 || c->cursor > total) c->cursor = total;  /* fresh or reset log: baseline, no replay */
    int k = 0;
    int abs = c->cursor;
    for (; abs < total; abs++) {
        int idx = reflux_host_index_for_abs(abs);
        if (idx < 0) continue;              /* overwritten before this poll */
        int type = reflux_host_action_type_at(idx);
        if (!reflux_action_is_horse(type)) continue;
        if (k >= max) break;                /* leave it for the next poll */
        out[k].type = type;
        out[k].a = reflux_host_action_a_at(idx);
        out[k].b = reflux_host_action_b_at(idx);
        out[k].c = reflux_host_action_c_at(idx);
        k++;
    }
    c->cursor = abs;
    return k;
}

#endif /* HORSE_API_H */
