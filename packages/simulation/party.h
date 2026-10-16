/* party.h -- the live day server's real party system: roster mutation (form/invite/kick/leave,
 * leadership transfer) plus range-gated XP splitting. EMILY/BACKLOG.md SECTION 536 follow-up,
 * founder real-time (2026-09-23/24): "add full party system parity use the GFD server subsystems"
 * -> "PARENA POWER EVERYTHING".
 *
 * Reverse-ported from GoblinFoxDragon's own real, already-shipped server/party/party.go (the
 * DragonsNShit MUD's FFXI-parity 6-player party system) -- same real behavior, adapted from GFD's
 * own string player-slot identifiers to BIG_O's own int PC_MAX_PLAYERS slot-index convention
 * (already established by every other live system in this repo, e.g. ServerRegulator's own
 * "target a player slot" model). The pure eligibility/XP-math decisions (day/packages/simulation/
 * party_rules.c, generated from PARENA/stdlib/big_o/party_rules.prn) are the same "mods first
 * everything" split every other BIG_O PARENA module already draws -- this header owns the actual
 * roster array and its mutation, which is inherently stateful/array-shaped, not scalar.
 *
 * Real, honest v1 scope cut, not guessed at: GFD's party.go also models Alliance (up to 3 parties,
 * 18 players) and XPChain (a consecutive-kill decay bonus) as genuinely separate concerns -- its
 * own doc comment says so explicitly. Neither is ported here; both are real, separate, bigger
 * scope, named as follow-ups in NORTHSTAR.md rather than folded in blind.
 *
 * Pure C99, no GL/SDL/network -- same "state in, mutate, read back" contract phone.h/
 * papercraft_inventory.h already established. The host (day/apps/server/src/main.c) owns the
 * actual g_parties[] array and turns party_* return codes into real wire packets/logs.
 */
#ifndef PARTY_H
#define PARTY_H

#include <string.h>

#define PARTY_MAX_MEMBERS 5 /* mirrors party_rules.c's own party_max_members() (non-leader slots) */

/* Real, generated PARENA decisions -- day/packages/simulation/party_rules.c, built from
   PARENA/stdlib/big_o/party_rules.prn. Declared here rather than #included as a header because the
   generated file is a plain .c translation unit (same convention walkie_rules.c/item_drop_mod.c
   already use), linked in by scripts/build_day.sh. */
int party_max_size(void);
int party_max_members(void);
int party_ok(void);
int party_err_not_leader(void);
int party_err_full(void);
int party_err_already_in_party(void);
int party_err_not_in_party(void);
int party_err_cannot_kick_self(void);
int party_full(int size);
int party_can_invite(int is_caller_leader, int size, int is_target_already_in_party);
int party_can_kick(int is_caller_leader, int is_target_self, int is_target_in_party);
int party_in_range(int distance_cm, int range_limit_cm);
int party_xp_share(int total_xp, int in_range_count);

/* ShankParty -- one real roster. leader_slot/members are PC_MAX_PLAYERS-space player slot indices
 * (this header stays index-space-agnostic on purpose -- it never reads PC_MAX_PLAYERS itself, same
 * "no upward dependency on the host's own wire constants" convention phone.h already follows).
 * active=0 means no party exists yet (never formed, or disbanded). */
typedef struct {
    int active;
    int leader_slot;
    int members[PARTY_MAX_MEMBERS]; /* join order */
    int member_count;
} ShankParty;

static inline void party_init(ShankParty *p) {
    memset(p, 0, sizeof(*p));
}

/* party_size -- leader + members, 0 if not active. */
static inline int party_size(const ShankParty *p) {
    return p->active ? (1 + p->member_count) : 0;
}

/* party_has -- true if slot is the leader or a member of an active party. */
static inline int party_has(const ShankParty *p, int slot) {
    if (!p->active) return 0;
    if (p->leader_slot == slot) return 1;
    for (int i = 0; i < p->member_count; i++) {
        if (p->members[i] == slot) return 1;
    }
    return 0;
}

/* party_invite -- party.go's own real "invite creates the party if the caller isn't already a
 * leader" convention: an inactive party becomes active with caller_slot as leader on the very first
 * invite. Returns a party_err_*() code, or party_ok() on success (target_slot is now a member).
 * caller_slot must already be the leader of an ACTIVE party for every invite after the first --
 * this function does not silently reassign leadership.
 */
static inline int party_invite(ShankParty *p, int caller_slot, int target_slot) {
    if (!p->active) {
        p->active = 1;
        p->leader_slot = caller_slot;
        p->member_count = 0;
    }
    int is_leader = (caller_slot == p->leader_slot);
    int already = party_has(p, target_slot);
    int size = party_size(p);
    int rc = party_can_invite(is_leader, size, already);
    if (rc != party_ok()) return rc;
    p->members[p->member_count++] = target_slot;
    return party_ok();
}

/* party_kick -- only the leader may kick, never themselves. Returns a party_err_*() code, or
 * party_ok() on success. */
static inline int party_kick(ShankParty *p, int caller_slot, int target_slot) {
    if (!p->active) return party_err_not_in_party();
    int is_leader = (caller_slot == p->leader_slot);
    int is_self = (target_slot == p->leader_slot);
    int is_member = party_has(p, target_slot);
    int rc = party_can_kick(is_leader, is_self, is_member);
    if (rc != party_ok()) return rc;
    for (int i = 0; i < p->member_count; i++) {
        if (p->members[i] == target_slot) {
            for (int j = i; j < p->member_count - 1; j++) p->members[j] = p->members[j + 1];
            p->member_count--;
            return party_ok();
        }
    }
    return party_err_not_in_party(); /* unreachable given the is_member check above; kept honest */
}

/* party_leave -- slot leaves voluntarily. If the leader leaves, leadership transfers to the
 * first remaining member in join order (party.go's own real rule) and the party stays active. If
 * the leader leaves with no members left, the party disbands. Returns 1 if the party is now
 * disbanded (caller should stop treating *p as active), 0 if it's still active. A non-member slot
 * is a real, honest no-op (returns 0, party unchanged) -- matches party.go's own ErrNotInParty
 * case, which the host simply never surfaces as an error here since "leave a party you're not in"
 * has no real consequence to report.
 */
static inline int party_leave(ShankParty *p, int slot) {
    if (!p->active) return 1;
    if (slot == p->leader_slot) {
        if (p->member_count == 0) {
            party_init(p);
            return 1;
        }
        p->leader_slot = p->members[0];
        for (int j = 0; j < p->member_count - 1; j++) p->members[j] = p->members[j + 1];
        p->member_count--;
        return 0;
    }
    for (int i = 0; i < p->member_count; i++) {
        if (p->members[i] == slot) {
            for (int j = i; j < p->member_count - 1; j++) p->members[j] = p->members[j + 1];
            p->member_count--;
            return 0;
        }
    }
    return 0;
}

/* party_xp_split -- party.go's own real XPSplit rule: fills out[] (leader first at index 0,
 * then members in join order, 1 + member_count entries total) with each member's even integer
 * share of total_xp among members within range_limit_cm of the kill point; out-of-range members
 * get 0. distances_cm is parallel to the same leader-then-members order, the host's own real
 * integer centimeters. out must have room for at least party_size(p) entries.
 */
static inline void party_xp_split(const ShankParty *p, int total_xp, const int *distances_cm,
                                        int range_limit_cm, int *out) {
    int n = party_size(p);
    int in_range = 0;
    for (int i = 0; i < n; i++) {
        out[i] = 0;
        if (party_in_range(distances_cm[i], range_limit_cm)) in_range++;
    }
    int each = party_xp_share(total_xp, in_range);
    for (int i = 0; i < n; i++) {
        if (party_in_range(distances_cm[i], range_limit_cm)) out[i] = each;
    }
}

#endif /* PARTY_H */
