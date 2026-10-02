#ifndef BIGO_AVIAN_VALUES_H
#define BIGO_AVIAN_VALUES_H

// avian_values.h -- "the birds": docs/DESIGN_DIGEST.md's Act II escalation ("the infection jumps
// species and makes birds hyper-intelligent, an organized avian coalition -- crows coordinating
// via birdsong ciphers, dropping acoustic beacons to pull feral hordes onto you"). Campaign
// content per NORTHSTAR.md §6 -- deferred as a *chapter*, but the coalition needs its own real,
// testable value vocabulary the same way zombies got one (core/zombie_values.h), not a reskinned
// NpcBrain or a reskinned ZombieState. Same "own RNG, own curve shape, no shared code with
// sibling value modules" convention zombie_values.h/giant_bug_values.h already establish.
//
// TYLER tie-in (the ask this module answers, "add the birds -- use TYLER"): NORTHSTAR.md §6 and
// docs/DESIGN_DIGEST.md line ~115 already flag that cross-repo canon exists and needs
// reconciling before hardening -- TYLER/README.md's Eastwind Owls ("the most complete timeline
// archive in existence... their inability to destroy records is a feature, not a bug") and
// TYLER's own recurring "BIRD CORRECTION PENDING" end-log line (TYLER/README.md, TYLER/
// CITY_OF_LIGHT.md, TYLER/0.md). The coalition modeled here is that reconciliation, not a fresh
// invention: BIG_O's avian faction is staffed, in-universe, by the same watching, archival,
// can't-look-away lineage as TYLER's Owls -- it doesn't destroy what it's seen, it *broadcasts*
// it (the beacon mechanic below), which is the Owls' own "they distribute it anyway because they
// cannot destroy records" line turned into a real, hostile game mechanic. "Bird Correction" is
// adopted here as the in-fiction name for the moment the coalition's own witness ledger forces a
// correction onto the world it's watching -- see docs/DESIGN_DIGEST.md's own updated Act II note
// for the fictional framing; this header is the mechanical layer under it.

#include <stdint.h>

typedef enum {
    AVIAN_MOOD_ROOSTING = 0,  // ambient, low alertness, not coordinating -- the coalition's resting state
    AVIAN_MOOD_SCOUTING,      // spotted something worth archiving; watching, not yet signaling
    AVIAN_MOOD_SIGNALING,     // actively broadcasting a birdsong-cipher beacon (see avian_beacon_strength)
    AVIAN_MOOD_MOBBING        // full coordinated harassment -- dive-bombing / dogging a marked target
} AvianMood;

typedef struct {
    float vigilance;     // 0.0-1.0 -- how much this bird has archived/witnessed; rises on avian_get_alerted,
                          // decays slowly otherwise. The Eastwind Owl "can't destroy records" property: this
                          // value never resets to 0, only ever approaches it asymptotically (see avian_tick).
    float coordination;  // 0.0-1.0 -- flock-network strength; rises while near other signaling/mobbing birds
                          // (caller supplies nearby_signaling_peers), decays when isolated. Gates SIGNALING.
    float exposure;      // 0.0-1.0 -- how conspicuous the coalition itself has become to the corporate
                          // night-society's own witness/Heat system (core/witness_rules.h) -- rises every
                          // tick spent in SIGNALING/MOBBING, the coalition's own cost for being witnessed.
                          // NOT fed into witness_rules.c yet (see this header's own integration-boundary
                          // note below), same "primitive first, wiring later" discipline as zombie_values.h.
    AvianMood mood;
    uint32_t mood_change_at_ms;  // next timer-driven mood re-evaluation, same pattern humanness.c/zombie_values.c use
} AvianState;

// avian_state_init -- fresh spawn: ROOSTING, vigilance/coordination/exposure all 0.0.
void avian_state_init(AvianState *a, uint32_t now_ms);

// avian_tick -- real, per-tick update. nearby_signaling_peers is the real, caller-supplied count
// of other birds currently in SIGNALING or MOBBING within this bird's own flock radius -- the
// only external input this module needs, deliberately narrow (no perception/geometry owned here).
// vigilance decays toward (never reaches) 0 by a fixed fraction each tick -- the Owl "can't
// destroy records" property, expressed as an asymptote rather than a hard floor. coordination
// rises with nearby_signaling_peers and decays otherwise; crossing AVIAN_COORD_THRESHOLD while
// vigilance is already high is what promotes SCOUTING -> SIGNALING (see avian_get_alerted for
// the ROOSTING -> SCOUTING edge, which needs a real discrete sighting, not just ambient drift).
void avian_tick(AvianState *a, uint32_t now_ms, int nearby_signaling_peers);

// avian_get_alerted -- a real, discrete sighting (a player or zombie enters this bird's real
// perception radius, caller's job to compute) forces at least SCOUTING and jumps vigilance
// toward 1.0 immediately (an Owl archiving a new record on the spot, not waiting for the next
// tick's drift) -- escalates an already-SIGNALING/MOBBING bird's target lock rather than
// downgrading it. Schedules a short re-evaluation window, same shape as
// zombie_get_agitated/humanness_get_startled.
void avian_get_alerted(AvianState *a, uint32_t now_ms);

// avian_beacon_strength -- 0.0-1.0, only nonzero in SIGNALING/MOBBING (0.0 at ROOSTING/SCOUTING).
// This IS the "acoustic beacon that pulls feral hordes onto you" from docs/DESIGN_DIGEST.md's Act
// II note: a real, continuous value meant to feed core/zombie_values.h's zombie_get_agitated as a
// probability/radius input at a future live-wiring pass (see this header's own integration
// boundary below) -- scales with coordination (a lone signaling bird is a weak beacon; a
// coordinated flock is a loud one) and gets a real MOBBING bonus over plain SIGNALING.
float avian_beacon_strength(const AvianState *a);

// avian_effective_alertness -- 0..100, this module's own real "how sharp is this bird's watch
// right now" analog to witness_rules.h's vigilance and zombie_values.h's zombie_effective_alertness.
// Baseline scales with mood (ROOSTING lowest, MOBBING highest) plus a real vigilance/coordination
// contribution, clamped to [0,100].
int avian_effective_alertness(const AvianState *a);

// Integration boundary, stated plainly (matching zombie_values.h's own convention): this module
// is NOT wired into core/witness_rules.c or core/zombie_values.c yet. avian_beacon_strength is a
// real, tested output value with a named future consumer (zombie_get_agitated, at a real live-
// wiring pass, same shape core/witness_live.h already did for zombie<->witness), not a second
// copy of an existing system and not yet a live effect on anything else in this repo. Act II
// content (the coalition as a playable chapter, per NORTHSTAR.md §6) stays deferred; this module
// only proves the coalition's own values/beacon math is real and testable ahead of that chapter.

#endif // BIGO_AVIAN_VALUES_H
