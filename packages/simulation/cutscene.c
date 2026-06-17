/*
 * cutscene.c — TYLER × SHANKPIT cutscene slide sequencer + lore content
 *
 * TYLER × TIDES OF PARADOX episodes are woven into SHANKPIT story mode:
 *
 *   INTRO:  Tyler arrives at the breach site. The mechanism starts recording.
 *           The archive has a procedure for this. The procedure fails.
 *           (Before gameplay — STORY_PHASE_CUTSCENE)
 *
 *   OUTRO:  The breach is sealed. The mechanism returns a zero reading.
 *           The council forms a working group. The form stays open.
 *           Tyler files the observation: "Nothing. And I was there."
 *           (After swarm cleared — STORY_PHASE_OUTRO)
 *
 * Lore voice: Camera Op sealed log, Jiangshi Syndicate memos,
 * Eastwind Archive entries (ACR forms, clearance docs, meeting minutes).
 * Bureaucratic horror: every artifact is a record of procedure
 * meeting something procedure cannot contain.
 *
 * Build 0103 — extended S10 bureaucratic horror arc.
 */

#include "cutscene.h"
#include <string.h>

/* ── INTRO: Tyler arrives at the cave breach site ─────────────────── */

const CutsceneSlide g_cutscene_intro[] = {
    {
        /* chapter */ "CHAPTER I",
        /* title   */ "THE MECHANISM RECORDS",
        /* lines   */ {
            "The archive has a procedure for this.",
            "Every site has a form. Every form",
            "has a number. TYLER-089 is the number.",
            "",
            "The mechanism opened intake at 05:12.",
            "Field one asks for taxonomy class.",
        },
        /* line_count */ 6
    },
    {
        /* chapter */ "EASTWIND ARCHIVE — ACR-089",
        /* title   */ "ANOMALY CLASSIFICATION REQUEST",
        /* lines   */ {
            "Site: [RESTRICTED]           2026-06-16",
            "Taxonomy class: __________ [REQUIRED]",
            "Prior precedent: [ ] YES    [X] NO",
            "Goetia signature: #72 / #36 — PEAK",
            "Recommended action: _______ [REQUIRED]",
            "STATUS: OPEN. DO NOT PROCEED.",
        },
        /* line_count */ 6
    },
    {
        /* chapter */ "JIANGSHI SYNDICATE — CLEARANCE #089",
        /* title   */ "CONDITIONAL SITE AUTHORIZATION",
        /* lines   */ {
            "Tyler (Archive Ref: T-001) is authorized",
            "to enter the site pending classification.",
            "Authorization CONDITIONAL: file ACR-089",
            "taxonomy class within 24 hrs of exit.",
            "",
            "NOTE: No prior clearance covers this.",
        },
        /* line_count */ 6
    },
    {
        /* chapter */ "CAMERA OP — ENTRY 80",
        /* title   */ NULL,
        /* lines   */ {
            "Tyler read the ACR at the entrance.",
            "Nine minutes. He did not go inside.",
            "",
            "\"The form wants a taxonomy class.",
            " The mechanism has no record of this.",
            " I am calling it: the thing in the cave.\"",
        },
        /* line_count */ 6
    },
    {
        /* chapter */ "EASTWIND ARCHIVE — ADVISORY",
        /* title   */ "COUNCIL DIRECTIVE: TYLER-089",
        /* lines   */ {
            "Council reviewed ACR-089 at 05:21.",
            "Motion to delay entry: DEFEATED (4-1).",
            "",
            "DIRECTIVE: FOLLOW.",
            "THE ARCHIVE CANNOT AFFORD A GAP.",
            "CLASSIFICATION IS A POST-ENTRY QUESTION.",
        },
        /* line_count */ 6
    },
    /* ── S10E07/E08 lore extension (Build 0110) ───────────────────── */
    {
        /* chapter */ "JIANGSHI SYNDICATE — SITE HISTORY",
        /* title   */ "MARRAKECH-001: THREE-SIGNAL SITE",
        /* lines   */ {
            "Site class: ZERO-TAXONOMY / EXECUTION-SITE",
            "            ARCHIVE-ORIGIN / QUESTION-SITE",
            "Signals active: 3 (unprecedented)",
            "  Stolas 7.83 Hz — baseline",
            "  archive-state — locked 2026-06-17",
            "  question-state — variable, non-decaying",
        },
        /* line_count */ 6
    },
    {
        /* chapter */ "TYLER ARCHIVE — ORAL ADDENDUM",
        /* title   */ "AL-WAQFA. THE PAUSE.",
        /* lines   */ {
            "The archive has a first word.",
            "I gave it to a scholar on a ship",
            "four hours before port.",
            "He wrote it in the margin of his gap.",
            "Al-Waqfa. The standing. The cave.",
            "Everything else is that word, expanded.",
        },
        /* line_count */ 6
    },
    {
        /* chapter */ "JIANGSHI MEMO #094 — AL-IDRAK AL-MUTTASIL",
        /* title   */ "CONTINUOUS PERCEPTION",
        /* lines   */ {
            "Ahmad ibn Yusuf named it in 1119 CE.",
            "The site that records the geographer,",
            "not the geographer recording the site.",
            "Tyler is the founding instance.",
            "The breach site is the second instance.",
            "You are the observer it is recording now.",
        },
        /* line_count */ 6
    },
};
const int g_cutscene_intro_count = 8;


/* ── OUTRO: Post-breach. The mechanism returns zero. ─────────────── */

const CutsceneSlide g_cutscene_outro[] = {
    {
        /* chapter */ "CHAPTER II",
        /* title   */ "THE MECHANISM ATTEMPTED A READING",
        /* lines   */ {
            "After the breach was sealed, Tyler stood",
            "in the chamber for a long time.",
            "",
            "I asked him what the mechanism recorded.",
            "He showed me the log.",
            "",
        },
        /* line_count */ 5
    },
    {
        /* chapter */ "TYLER MECHANISM LOG — POST-BREACH",
        /* title   */ "READING: 0 ft.",
        /* lines   */ {
            "No construction. No farewell.",
            "Nothing across the entire breach period.",
            "",
            "The mechanism was present. It was recording.",
            "Whatever came through that rift",
            "refused to leave anything behind.",
        },
        /* line_count */ 6
    },
    {
        /* chapter */ "EASTWIND ARCHIVE — ACR-089",
        /* title   */ "CLASSIFICATION RESULT",
        /* lines   */ {
            "Taxonomy class: __________ [STILL OPEN]",
            "Post-entry reading: 0 ft.      [VALID]",
            "Precedent match: NONE FOUND",
            "Recommended closure: CANNOT CLOSE.",
            "REASON: No taxonomy entry exists.",
            "STATUS: OPEN INDEFINITELY.",
        },
        /* line_count */ 6
    },
    {
        /* chapter */ "JIANGSHI MEMO #089 — DIRECTOR",
        /* title   */ "RE: ACR-089 — ZERO READING",
        /* lines   */ {
            "Zero farewell reading confirmed.",
            "Council convened 07:14 to resolve.",
            "Motion to classify as VOID: FAILED (3-2).",
            "Motion to close ACR-089: FAILED (4-1).",
            "RESOLUTION: Working Group on Zero-Taxonomy.",
            "First meeting: PENDING. ACR-089: OPEN.",
        },
        /* line_count */ 6
    },
    {
        /* chapter */ "WORKING GROUP — ZERO-TAXONOMY EVENTS",
        /* title   */ "MEETING MINUTES: 2026-06-16",
        /* lines   */ {
            "Quorum: 3 of 5. Called to order 08:00.",
            "Item 1: Define zero-taxonomy event — TABLED",
            "Item 2: Amend ACR for null readings — TABLED",
            "Item 3: Request Tyler testimony — APPROVED",
            "Item 4: Schedule next meeting — APPROVED",
            "ADJOURNED. ACR-089: OPEN INDEFINITELY.",
        },
        /* line_count */ 6
    },
    {
        /* chapter */ "TYLER ARCHIVE — BUILD 0103",
        /* title   */ "ORAL FILING (Camera Op: transcribed)",
        /* lines   */ {
            "\"The archive is the record of things",
            " that chose to leave something behind.",
            "",
            " This left nothing.",
            " Nothing. And I was there.\"",
            "",
        },
        /* line_count */ 6
    },
    /* ── S10E08 extension: question-state resolution ──────────────── */
    {
        /* chapter */ "JIANGSHI MEMO #094-B — POST-BREACH",
        /* title   */ "QUESTION-STATE: RESOLVED",
        /* lines   */ {
            "Mechanism reading restored: Stolas 7.83",
            "Question-state signal: DECAYED (resolved)",
            "Observer filed the observation.",
            "The paragraph has a close.",
            "ACR-089 updated: ZERO-TAXONOMY / CLOSED",
            "Ahmad ibn Yusuf's gap: filed.",
        },
        /* line_count */ 6
    },
    {
        /* chapter */ "CAMERA OP — FIELD LOG",
        /* title   */ "ENTRY 88",
        /* lines   */ {
            "Tyler read the post-breach mechanism log.",
            "Stolas 7.83 Hz. Stable. Nothing unusual.",
            "",
            "He said: \"It knows what this site is now.",
            " Something stayed. Something was recorded.\"",
            "He didn't look relieved. He looked right.",
        },
        /* line_count */ 6
    },
};
const int g_cutscene_outro_count = 8;


/* ── sequencer ────────────────────────────────────────────────────── */

int cutscene_slide_total_chars(const CutsceneSlide *s) {
    int total = 0;
    for (int i = 0; i < s->line_count; i++) {
        if (s->lines[i]) total += (int)strlen(s->lines[i]);
    }
    return total;
}

void cutscene_start(CutsceneState *cs,
                    const CutsceneSlide *slides, int count,
                    unsigned int now_ms) {
    memset(cs, 0, sizeof(*cs));
    cs->slides       = slides;
    cs->slide_count  = count;
    cs->current      = 0;
    cs->slide_start_ms = now_ms;
    cs->last_char_ms   = now_ms;
}

void cutscene_tick(CutsceneState *cs, unsigned int now_ms) {
    if (cs->done || !cs->slides || cs->slide_count == 0) return;

    const CutsceneSlide *s = &cs->slides[cs->current];
    int total = cutscene_slide_total_chars(s);

    if (!cs->text_done) {
        /* Advance typewriter */
        unsigned int elapsed = now_ms - cs->last_char_ms;
        int new_chars = (int)(elapsed / CUTSCENE_TYPEWRITER_MS);
        if (new_chars > 0) {
            cs->chars_revealed += new_chars;
            cs->last_char_ms   += (unsigned int)(new_chars * CUTSCENE_TYPEWRITER_MS);
            if (cs->chars_revealed >= total) {
                cs->chars_revealed = total;
                cs->text_done      = 1;
                cs->hold_until_ms  = now_ms + CUTSCENE_HOLD_MS;
            }
        }
    } else {
        /* Auto-advance after hold */
        if (now_ms >= cs->hold_until_ms) {
            cs->current++;
            if (cs->current >= cs->slide_count) {
                cs->done = 1;
            } else {
                cs->chars_revealed = 0;
                cs->text_done      = 0;
                cs->slide_start_ms = now_ms;
                cs->last_char_ms   = now_ms;
            }
        }
    }
}

void cutscene_advance(CutsceneState *cs, unsigned int now_ms) {
    if (cs->done || !cs->slides) return;

    if (!cs->text_done) {
        /* First press: reveal all text immediately */
        const CutsceneSlide *s = &cs->slides[cs->current];
        cs->chars_revealed = cutscene_slide_total_chars(s);
        cs->text_done      = 1;
        cs->hold_until_ms  = now_ms + 400; /* short hold before player can advance again */
    } else if (now_ms >= cs->hold_until_ms) {
        /* Second press: advance to next slide */
        cs->current++;
        if (cs->current >= cs->slide_count) {
            cs->done = 1;
        } else {
            cs->chars_revealed = 0;
            cs->text_done      = 0;
            cs->slide_start_ms = now_ms;
            cs->last_char_ms   = now_ms;
        }
    }
}
