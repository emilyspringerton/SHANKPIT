/* party_test.c -- real, direct coverage for party.h's own roster mutation (invite/kick/
 * leave/leadership-transfer) and XP-split decisions, against the real, live PARENA-compiled
 * party_rules.c (not a mock) -- same real cross-package shape bigo_walkie_talkie_test.c already
 * establishes. Faithfully checks GFD's own real server/party/party.go behavior, reverse-ported.
 */
#include "./party.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    ShankParty p;
    party_init(&p);
    assert(party_size(&p) == 0);
    assert(!p.active);
    printf("PASS: a fresh party is inactive, size 0\n");

    /* First invite forms the party, caller as leader. */
    assert(party_invite(&p, 10, 11) == party_ok());
    assert(p.active && p.leader_slot == 10 && p.member_count == 1 && p.members[0] == 11);
    assert(party_size(&p) == 2);
    printf("PASS: the first invite forms the party with the caller as leader\n");

    /* Only the leader may invite. */
    assert(party_invite(&p, 11, 12) == party_err_not_leader());
    assert(p.member_count == 1);
    printf("PASS: a non-leader member cannot invite, real roster unchanged\n");

    /* Cannot invite someone already in the party. */
    assert(party_invite(&p, 10, 11) == party_err_already_in_party());
    printf("PASS: inviting an already-present member is rejected\n");

    /* Fill to capacity (leader + 5 members = 6 = party_max_size()). */
    assert(party_invite(&p, 10, 12) == party_ok());
    assert(party_invite(&p, 10, 13) == party_ok());
    assert(party_invite(&p, 10, 14) == party_ok());
    assert(party_invite(&p, 10, 15) == party_ok());
    assert(party_size(&p) == party_max_size());
    assert(party_invite(&p, 10, 16) == party_err_full());
    printf("PASS: a full party (%d/%d, GFD's own real MaxPartySize) rejects further invites\n",
           party_size(&p), party_max_size());

    /* Only the leader may kick; the leader cannot kick themselves. */
    assert(party_kick(&p, 11, 12) == party_err_not_leader());
    assert(party_kick(&p, 10, 10) == party_err_cannot_kick_self());
    assert(party_kick(&p, 10, 99) == party_err_not_in_party());
    assert(party_kick(&p, 10, 15) == party_ok());
    assert(party_size(&p) == 5);
    assert(!party_has(&p, 15));
    printf("PASS: leader-only kick, no self-kick, not-a-member rejected; real kick removes the member\n");

    /* A regular member leaving doesn't disband or change leadership. */
    assert(party_leave(&p, 13) == 0);
    assert(p.leader_slot == 10);
    assert(!party_has(&p, 13));
    printf("PASS: a member leaving voluntarily doesn't disband or transfer leadership\n");

    /* Leader leaving transfers leadership to the first remaining member in join order. */
    int members_before[8];
    int n = 0;
    for (int i = 0; i < p.member_count; i++) members_before[n++] = p.members[i];
    int expected_new_leader = members_before[0];
    assert(party_leave(&p, 10) == 0);
    assert(p.active);
    assert(p.leader_slot == expected_new_leader);
    assert(!party_has(&p, 10));
    printf("PASS: the leader leaving transfers leadership to the first remaining member (real slot %d), party stays active\n",
           expected_new_leader);

    /* Leaving down to nothing disbands. */
    ShankParty p2;
    party_init(&p2);
    assert(party_invite(&p2, 20, 21) == party_ok());
    assert(party_leave(&p2, 21) == 0); /* the one real member leaves, leader stays solo */
    assert(party_size(&p2) == 1);
    assert(party_leave(&p2, 20) == 1); /* now the leader leaves alone -- real disband */
    assert(!p2.active);
    assert(party_size(&p2) == 0);
    printf("PASS: the leader leaving an otherwise-empty party really disbands it\n");

    /* Leaving a party you're not in is a real, honest no-op. */
    ShankParty p3;
    party_init(&p3);
    assert(party_leave(&p3, 77) == 1); /* inactive party -- treated as already-disbanded */
    printf("PASS: leaving an inactive/nonexistent party is a safe no-op\n");

    /* XP split -- GFD's own real party.go XPSplit rule: even integer division among in-range
       members only, remainder discarded, zero in-range members shares zero. */
    ShankParty px;
    party_init(&px);
    assert(party_invite(&px, 30, 31) == party_ok());
    assert(party_invite(&px, 30, 32) == party_ok());
    /* leader(30) close, member 31 close, member 32 far. */
    int distances_cm[3] = { 100, 200, 5000 };
    int shares[3];
    party_xp_split(&px, 300, distances_cm, 1000, shares);
    assert(shares[0] == 150 && shares[1] == 150 && shares[2] == 0);
    printf("PASS: XP splits evenly among in-range members (150/150/0), out-of-range member gets nothing\n");

    /* Remainder is discarded, not distributed -- party.go's own real integer-division rule. */
    party_xp_split(&px, 301, distances_cm, 1000, shares);
    assert(shares[0] == 150 && shares[1] == 150 && shares[2] == 0);
    printf("PASS: XP-split remainder is discarded (301/2 = 150, not 150/151), matching GFD's own real rule\n");

    /* Nobody in range -- everybody shares zero, no divide-by-zero. */
    int far_distances[3] = { 9000, 9000, 9000 };
    party_xp_split(&px, 300, far_distances, 1000, shares);
    assert(shares[0] == 0 && shares[1] == 0 && shares[2] == 0);
    printf("PASS: zero in-range members shares zero for everyone, no divide-by-zero\n");

    printf("\nALL PASS\n");
    return 0;
}
