// avian_values.c -- see avian_values.h.
#include "avian_values.h"

#define AVIAN_VIGILANCE_DECAY_FRACTION 0.03f  /* asymptotic decay toward 0, never reaches it */
#define AVIAN_VIGILANCE_ALERT_JUMP 0.85f      /* real discrete-sighting jump target */
#define AVIAN_COORD_RISE_PER_PEER 0.25f       /* per nearby signaling/mobbing peer, per tick */
#define AVIAN_COORD_DECAY_PER_SEC 0.10f       /* isolated flock loses network strength */
#define AVIAN_COORD_SIGNAL_THRESHOLD 0.4f     /* SCOUTING -> SIGNALING gate */
#define AVIAN_VIGILANCE_SIGNAL_THRESHOLD 0.5f /* SIGNALING also needs real vigilance, not just peers */
#define AVIAN_COORD_MOB_THRESHOLD 0.75f       /* SIGNALING -> MOBBING gate */
#define AVIAN_EXPOSURE_RISE_PER_SEC 0.06f      /* cost of being witnessed while signaling/mobbing */
#define AVIAN_EXPOSURE_DECAY_PER_SEC 0.02f     /* slow fade once quiet again */

void avian_state_init(AvianState *a, uint32_t now_ms) {
    a->vigilance = 0.0f;
    a->coordination = 0.0f;
    a->exposure = 0.0f;
    a->mood = AVIAN_MOOD_ROOSTING;
    a->mood_change_at_ms = now_ms + 1000u; /* first real evaluation one second out */
}

void avian_tick(AvianState *a, uint32_t now_ms, int nearby_signaling_peers) {
    /* Same flat ~1-real-second-per-call convention zombie_tick documents; callers on a faster
       tick should call this less often than every physics tick, not every frame. */
    const float dt_sec = 1.0f;

    /* Vigilance is a real asymptotic decay -- the Owl "can't destroy records" property: it
       approaches 0.0 but a single ongoing decay step never actually reaches it exactly, matching
       this header's own documented contract (multiplicative shrink, not a subtractive floor). */
    a->vigilance -= a->vigilance * AVIAN_VIGILANCE_DECAY_FRACTION * dt_sec;
    if (a->vigilance < 0.0001f) a->vigilance = 0.0001f;

    if (nearby_signaling_peers > 0) {
        a->coordination += AVIAN_COORD_RISE_PER_PEER * (float)nearby_signaling_peers * dt_sec;
    } else {
        a->coordination -= AVIAN_COORD_DECAY_PER_SEC * dt_sec;
    }
    if (a->coordination < 0.0f) a->coordination = 0.0f;
    if (a->coordination > 1.0f) a->coordination = 1.0f;

    if (a->mood == AVIAN_MOOD_SIGNALING || a->mood == AVIAN_MOOD_MOBBING) {
        a->exposure += AVIAN_EXPOSURE_RISE_PER_SEC * dt_sec;
    } else {
        a->exposure -= AVIAN_EXPOSURE_DECAY_PER_SEC * dt_sec;
    }
    if (a->exposure < 0.0f) a->exposure = 0.0f;
    if (a->exposure > 1.0f) a->exposure = 1.0f;

    if (now_ms < a->mood_change_at_ms) return;

    /* MOBBING only ever holds while coordination stays past the mob threshold -- drops back to
       SIGNALING once the flock loses that much cohesion, same "spike sustained by a real
       condition, not a one-way ratchet" shape zombie_tick's own FRENZIED handling uses. */
    if (a->mood == AVIAN_MOOD_MOBBING) {
        if (a->coordination < AVIAN_COORD_MOB_THRESHOLD) a->mood = AVIAN_MOOD_SIGNALING;
    } else if (a->coordination >= AVIAN_COORD_MOB_THRESHOLD &&
               a->vigilance >= AVIAN_VIGILANCE_SIGNAL_THRESHOLD) {
        a->mood = AVIAN_MOOD_MOBBING;
    } else if (a->coordination >= AVIAN_COORD_SIGNAL_THRESHOLD &&
               a->vigilance >= AVIAN_VIGILANCE_SIGNAL_THRESHOLD) {
        a->mood = AVIAN_MOOD_SIGNALING;
    } else if (a->vigilance >= 0.15f) {
        a->mood = AVIAN_MOOD_SCOUTING;
    } else {
        a->mood = AVIAN_MOOD_ROOSTING;
    }
    a->mood_change_at_ms = now_ms + 1000u; /* real 1s re-evaluation cadence */
}

void avian_get_alerted(AvianState *a, uint32_t now_ms) {
    a->vigilance = AVIAN_VIGILANCE_ALERT_JUMP;
    if (a->mood == AVIAN_MOOD_ROOSTING) a->mood = AVIAN_MOOD_SCOUTING;
    /* SIGNALING/MOBBING birds stay put -- a fresh sighting sharpens an already-coordinating
       flock's lock, it doesn't downgrade it. Only ever PULLS the next evaluation earlier, never
       pushes it later -- a caller re-alerting every tick (a sustained sighting) must not starve
       avian_tick's own mood re-evaluation forever by repeatedly rescheduling it into the future. */
    uint32_t candidate = now_ms + 500u; /* short real window, matching zombie_get_agitated */
    if (candidate < a->mood_change_at_ms) a->mood_change_at_ms = candidate;
}

float avian_beacon_strength(const AvianState *a) {
    if (a->mood != AVIAN_MOOD_SIGNALING && a->mood != AVIAN_MOOD_MOBBING) return 0.0f;
    float strength = a->coordination; /* a lone signaling bird is a weak beacon */
    if (a->mood == AVIAN_MOOD_MOBBING) strength = strength * 0.5f + 0.5f; /* real, floored-up bonus */
    if (strength > 1.0f) strength = 1.0f;
    return strength;
}

int avian_effective_alertness(const AvianState *a) {
    float base;
    switch (a->mood) {
        case AVIAN_MOOD_ROOSTING:  base = 15.0f; break;
        case AVIAN_MOOD_SCOUTING:  base = 45.0f; break;
        case AVIAN_MOOD_SIGNALING: base = 70.0f; break;
        case AVIAN_MOOD_MOBBING: default: base = 95.0f; break;
    }
    float v = base + a->vigilance * 10.0f + a->coordination * 10.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 100.0f) v = 100.0f;
    return (int)(v + 0.5f);
}
