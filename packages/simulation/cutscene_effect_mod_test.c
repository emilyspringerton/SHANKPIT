/* cutscene_effect_mod_test.c -- real test for the real PARENA-compiled cutscene_effect_mod.c
 * (packages/simulation/cutscene_effect_mod.c, generated from
 * PARENA/stdlib/tyler/cutscene_mod.prn). Not yet wired into a live host -- see that .prn file's
 * own header comment for the real, honest current blocker (a live Go host call needs either cgo
 * or PARENA's own Go emitter, project BURROW, which is scoped but not built yet). This test
 * verifies the compiled decision logic directly, same real "verified, not just written" bar every
 * other real PARENA mod in this monorepo already holds itself to.
 *
 * Real, honest deliberate NOT-ladybug note (founder real-time, 2026-08-30, standing instruction:
 * "when you add new tests use the ladybug bdd framework"): checked before writing this file --
 * ladybug's own real, documented current status (ladybug/README.md's own "Honest current status"
 * section) is that its scarab.prn runner (describe/context/it) and firefly/ladybug.prn matcher
 * chain (expect/to/equal) do NOT yet pass VS0 domain 3 (parena build, the C emitter) at all --
 * blocked on real, pre-existing PARENA compiler gaps (Vec-as-generic-field, &Any/&mut T reference
 * types, non-zero-arg Fn callback params, multi-field defenum payloads) that sit in ladybug's own
 * framework plumbing itself, not in any one consuming test's complexity -- meaning no real
 * consumer, however simple, can get a working `parena build`-compiled ladybug test today. This
 * plain C assert() harness (matching level_mod_test.c/xp_award_mod_test.c's own real, already-
 * proven convention) is the honest, working fallback until those domain-3 gaps close; migrate
 * this file (and every other real PARENA mod test in this monorepo) to ladybug once they do --
 * tracked in EMILY/BACKLOG.md, not silently deferred.
 */
#include <assert.h>
#include <stdio.h>

int cutscene_effect_noop(void);
int cutscene_effect_advance_stage(void);
int cutscene_effect_trigger_awareness(void);
int cutscene_effect_grant_contact(void);
int cutscene_effect_post_message(void);
int on_tyler_cutscene_effect_is_mechanical(int effect_code);
int on_tyler_cutscene_trust_threshold_met(int player_trust, int required_trust);
int on_tyler_cutscene_urgency_window_met(int event_live_elapsed, int urgency_threshold);
int on_tyler_cutscene_trigger_ready(int player_trust, int required_trust, int one_shot, int already_seen);

int main(void) {
    /* Real effect codes match tyler_cutscene_system.md's own "Effects (choice outcomes)" table
       verbatim: noop=0, advance_stage=1, trigger_awareness=2, grant_contact=3, post_message=4. */
    assert(cutscene_effect_noop() == 0);
    assert(cutscene_effect_advance_stage() == 1);
    assert(cutscene_effect_trigger_awareness() == 2);
    assert(cutscene_effect_grant_contact() == 3);
    assert(cutscene_effect_post_message() == 4);

    /* noop is the one real effect code with no mechanical consequence; every other real code
       requires the host to actually mutate persistent state. */
    assert(on_tyler_cutscene_effect_is_mechanical(cutscene_effect_noop()) == 0);
    assert(on_tyler_cutscene_effect_is_mechanical(cutscene_effect_advance_stage()) == 1);
    assert(on_tyler_cutscene_effect_is_mechanical(cutscene_effect_trigger_awareness()) == 1);
    assert(on_tyler_cutscene_effect_is_mechanical(cutscene_effect_grant_contact()) == 1);
    assert(on_tyler_cutscene_effect_is_mechanical(cutscene_effect_post_message()) == 1);

    /* Real trigger condition #1: faction trust threshold, server-evaluated. */
    assert(on_tyler_cutscene_trust_threshold_met(50, 50) == 1);   /* exactly at threshold */
    assert(on_tyler_cutscene_trust_threshold_met(49, 50) == 0);   /* one short */
    assert(on_tyler_cutscene_trust_threshold_met(100, 50) == 1);  /* well past */

    /* Real trigger condition #2: time-based urgency escalation. */
    assert(on_tyler_cutscene_urgency_window_met(300000, 300000) == 1); /* exactly at threshold */
    assert(on_tyler_cutscene_urgency_window_met(299999, 300000) == 0); /* one ms short */
    assert(on_tyler_cutscene_urgency_window_met(600000, 300000) == 1); /* well past */

    /* Real, composed trigger-readiness: one-shot + already-seen blocks the trust gate entirely,
       regardless of how high trust actually is. */
    assert(on_tyler_cutscene_trigger_ready(100, 50, 1, 1) == 0);  /* one-shot, already seen: never fires again */
    assert(on_tyler_cutscene_trigger_ready(100, 50, 1, 0) == 1);  /* one-shot, not yet seen, trust met: fires */
    assert(on_tyler_cutscene_trigger_ready(30, 50, 1, 0) == 0);   /* one-shot, not yet seen, trust NOT met */
    assert(on_tyler_cutscene_trigger_ready(100, 50, 0, 1) == 1);  /* not one-shot: already-seen is irrelevant */
    assert(on_tyler_cutscene_trigger_ready(30, 50, 0, 1) == 0);   /* not one-shot, trust still not met */

    printf("cutscene_effect_mod_test: all assertions passed\n");
    return 0;
}
