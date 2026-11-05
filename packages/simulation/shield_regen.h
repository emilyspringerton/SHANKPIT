#ifndef SHIELD_REGEN_H
#define SHIELD_REGEN_H

/* shield_regen.h -- the HOST half of a broken shield coming back.
 *
 * ../PARENA/stdlib/shankpit/shield_regen_rules.prn owns the rule (four seconds of vulnerability after
 * the last hit, then a one-second tick that starts at 1 point and grows to 10); this header owns the
 * two things a host has to do with it:
 *
 *   1. REMEMBER what the rule needs. PlayerState carries the shield the body held when damage last
 *      landed (shield_regen_base) and the time it landed (shield_regen_at_ms). That pair IS the
 *      counter: a hit rewrites both, so "the four seconds restart" needs no reset code of its own.
 *   2. ADVANCE the shield once per tick. shield_regen_tick() recomputes it from that pair, so calling
 *      it twice in a tick, or missing a tick, cannot double-count or bank extra points -- the answer
 *      is a function of elapsed time, not of how many times it was called.
 *
 * A hit MUST be reported with shield_regen_note_damage() AFTER the shield has absorbed its share (see
 * the call sites in physics.h/local_game.h). Snapshotting before the subtraction would let the regen
 * restore the shield the hit had already removed.
 *
 * The HUD needs no state of its own: shield_regen_counter_tenths() is the 4-second countdown and
 * shield_regen_next_amount() is what the next tick will be worth, both read from the same pair the
 * simulation uses, so the bar, the countdown and the real shield cannot drift apart.
 *
 * TICK LENGTH: the rule is in milliseconds, so a host passes real elapsed time (the engine's cmd_time,
 * the server's now_ms). A host that only counts frames converts on its own side with
 * sh_regen_delay_ticks()/sh_regen_interval_ticks(), which round up. */

#include "protocol.h"
#include "shield_regen_rules.h"

/* A host-installed curve for one body (a character mod's own tick value). Same shape as physics.h's
 * g_phys_stats_health_hook precedent: PARENA owns the default rule, the host owns which body overrides
 * it. Return the tick's value, or <= 0 for "no opinion, use the rule". The hook is read for the ramp's
 * steps (0..cap) and the last step repeats beyond the cap, which is exactly how the default curve is
 * flat after the cap -- so a mod's curve cannot make the per-tick cost unbounded. */
typedef int (*ShieldRegenAmountHook)(const PlayerState *p, int tick_index);

static ShieldRegenAmountHook shield_regen_amount_hook = NULL;

static inline void shield_regen_set_amount_hook(ShieldRegenAmountHook fn) {
    shield_regen_amount_hook = fn;
}

/* What tick `index` is worth to this body: the mod's number when it has one, the PARENA rule otherwise. */
static inline int shield_regen_amount_for(const PlayerState *p, int index) {
    if (shield_regen_amount_hook) {
        int v = shield_regen_amount_hook(p, index);
        if (v > 0) return v;
    }
    return sh_regen_tick_amount(index);
}

/* Every landed tick's points, through the installed curve. With no hook this IS sh_regen_total (the
 * closed form); with one, the ramp's steps are asked individually and the post-cap flat step repeats. */
static inline int shield_regen_total_for(const PlayerState *p, int since_ms) {
    int n, cap, i, total = 0;
    if (!shield_regen_amount_hook) return sh_regen_total(since_ms);
    n = sh_regen_ticks_landed(since_ms);
    cap = shield_regen_ramp_cap();
    for (i = 0; i < n && i < cap; i++) total += shield_regen_amount_for(p, i);
    if (n > cap) total += (n - cap) * shield_regen_amount_for(p, cap);
    return total;
}

/* Ten minutes. A clock that jumped backwards (respawn, a level load, a wrapped unsigned) would
 * otherwise read as an enormous elapsed time and hand back a full bar from nothing; capping the
 * elapsed time keeps a stale anchor harmless instead of generous. */
#define SHIELD_REGEN_MAX_ELAPSED_MS 600000

/* Call after damage has been applied to the shield: this is the body's new regen baseline and t=0 for
 * its four seconds. A hit during the window simply calls this again, which is the restart. */
static inline void shield_regen_note_damage(PlayerState *p, unsigned int now_ms) {
    if (!p) return;
    if (p->shield < 0) p->shield = 0;
    p->shield_regen_base = p->shield;
    p->shield_regen_at_ms = now_ms;
    p->shield_regen_timer = 0;
}

/* How long since the last hit started the clock, clamped so a stale anchor cannot invent a bar. */
static inline int shield_regen_since_ms(const PlayerState *p, unsigned int now_ms) {
    unsigned int since;
    /* A clock that reads EARLIER than the anchor means the two numbers came from different clocks
     * (a client cmd_time against a server now_ms, a reloaded level). Reading that as unsigned would
     * wrap to ~49 days and hand back a full bar from nothing, so the honest answer is "no time has
     * passed": the regen stalls for one frame instead of paying out a whole shield. */
    if (now_ms < p->shield_regen_at_ms) return 0;
    since = now_ms - p->shield_regen_at_ms;
    return since > (unsigned int)SHIELD_REGEN_MAX_ELAPSED_MS ? SHIELD_REGEN_MAX_ELAPSED_MS : (int)since;
}

/* Once per tick: put the shield where the rule says it should be. A body already at the bar only has
 * its anchor refreshed (there is nothing to regenerate, and a later hit must not inherit an old
 * clock). Uses the elapsed time, so a duplicated call is a no-op. */
static inline void shield_regen_tick(PlayerState *p, unsigned int now_ms) {
    int since;
    if (!p) return;
    since = shield_regen_since_ms(p, now_ms);
    if (p->shield >= shield_max()) {
        p->shield_regen_base = p->shield;
        p->shield_regen_at_ms = now_ms;
        p->shield_regen_timer = 0;
        return;
    }
    p->shield_regen_timer = since;                 /* the ms-since-hit mirror the old field carried */
    p->shield = sh_clamp(0, shield_max(), p->shield_regen_base + shield_regen_total_for(p, since));
}

/* How far a client's clock may sit from the server's before it is snapped back. Under this the local
 * anchor is KEPT, so the countdown (and the next tick) advance continuously between packets instead of
 * being re-phased by network jitter -- the one thing that made a live countdown visibly stick and step
 * backwards while the server's own clock kept running. */
#define SHIELD_REGEN_RESYNC_MS 150

/* Client half: adopt the server's regen clock (NetPlayer.shield_regen_since_ms) on ONE snapshot.
 *
 * wire_since_ms is elapsed ms, not a deadline, so it is re-anchored on THIS host's clock. A packet is
 * the truth but it is not a metronome: adopting it by simply resetting the anchor every time makes the
 * countdown jump by the packet's own arrival jitter (and by however coarsely the sender sampled the
 * number). So the anchor only moves when there is really something to resync to -- a fresh hit (wire 0),
 * a clock that has run backwards, or more than SHIELD_REGEN_RESYNC_MS of drift (lost packets, a long
 * stall). Inside that, the local anchor stays and the local clock interpolates between snapshots.
 *
 * The baseline is derived from the shield the server has ALREADY regenerated at this instant, using the
 * elapsed time the caller ends up believing in -- taking the wire shield AS the baseline would pay every
 * landed tick a second time, and deriving it from the wire's elapsed time while keeping a local anchor
 * would make the bar jump forward by the drift on every packet. */
static inline void shield_regen_adopt_clock(PlayerState *p, int wire_since_ms, int wire_shield,
                                            unsigned int now_ms) {
    int local_since;
    if (!p) return;
    if (wire_since_ms < 0) wire_since_ms = 0;
    if (wire_since_ms > SHIELD_REGEN_MAX_ELAPSED_MS) wire_since_ms = SHIELD_REGEN_MAX_ELAPSED_MS;
    local_since = shield_regen_since_ms(p, now_ms);
    if (wire_since_ms == 0 || p->shield_regen_at_ms > now_ms ||
        local_since - wire_since_ms > SHIELD_REGEN_RESYNC_MS ||
        wire_since_ms - local_since > SHIELD_REGEN_RESYNC_MS) {
        p->shield_regen_at_ms = now_ms - (unsigned int)wire_since_ms;
        local_since = wire_since_ms;
    }
    p->shield_regen_base = sh_clamp(0, shield_max(), wire_shield - shield_regen_total_for(p, local_since));
    p->shield_regen_timer = local_since;
}

/* The 4-second countdown for the HUD: ms left before the first tick, 0 once regen has started. */
static inline int shield_regen_counter_ms(const PlayerState *p, unsigned int now_ms) {
    return sh_regen_in_ms(shield_regen_since_ms(p, now_ms));
}

/* 1 while the body is still inside its four seconds of vulnerability. */
static inline int shield_regen_waiting(const PlayerState *p, unsigned int now_ms) {
    return sh_regen_waiting_(shield_regen_since_ms(p, now_ms));
}

/* What the NEXT tick will be worth (1, 2, 3, ... 10) -- the ramp, for a HUD that wants to show it.
 * Goes through the installed curve, so a HUD shows the number the body will really get. */
static inline int shield_regen_next_amount(const PlayerState *p, unsigned int now_ms) {
    return shield_regen_amount_for(p, sh_regen_ticks_landed(shield_regen_since_ms(p, now_ms)));
}

/* The 4-second counter as TENTHS OF A SECOND for a HUD that draws one decimal: 40 at the hit, 1 with
 * 1 ms left, 0 once the ticks start. Rounded UP like the tick helpers, so the display never reaches
 * zero before the shield actually moves. */
static inline int shield_regen_counter_tenths(const PlayerState *p, unsigned int now_ms) {
    return (shield_regen_counter_ms(p, now_ms) + 99) / 100;
}

/* The bar's fill in permille (0..1000), so a renderer does not need to know the cap. */
static inline int shield_regen_bar_permille(const PlayerState *p) {
    int m = shield_max();
    return m > 0 ? (p->shield * 1000) / m : 0;
}

#endif /* SHIELD_REGEN_H */
