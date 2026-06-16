/*
 * cutscene.c — TYLER × SHANKPIT cutscene slide sequencer + lore content
 *
 * TYLER × TIDES OF PARADOX episodes are woven into SHANKPIT story mode:
 *
 *   INTRO:  Tyler arrives at the breach site. The mechanism starts recording.
 *           (Before gameplay — STORY_PHASE_CUTSCENE)
 *
 *   OUTRO:  The breach is sealed. The mechanism returns a zero reading.
 *           Tyler files the observation: "Nothing. And I was there."
 *           (After swarm cleared — STORY_PHASE_OUTRO)
 *
 * All lore content is written in the TYLER in-universe voice:
 * Camera Op sealed log, Jiangshi Syndicate memos, Eastwind archive entries.
 * These are extensions of the canonical Tyler archive (post Build 0102).
 */

#include "cutscene.h"
#include <string.h>

/* ── INTRO: Tyler arrives at the cave breach site ─────────────────── */

const CutsceneSlide g_cutscene_intro[] = {
    {
        /* chapter */ "CHAPTER I",
        /* title   */ "THE MECHANISM RECORDS",
        /* lines   */ {
            "I arrived at the cave before sunrise.",
            "The mechanism was already active — recording,",
            "cataloguing, adding to the archive the way",
            "it has done in every city since 1127.",
            "",
            "But what it found inside had no name in the taxonomy.",
        },
        /* line_count */ 6
    },
    {
        /* chapter */ "CAMERA OP — ENTRY 80",
        /* title   */ NULL,
        /* lines   */ {
            "Tyler stood at the entrance for nine minutes.",
            "He did not go inside.",
            "",
            "\"The archive logs construction.",
            " It logs departure. It logs farewell.",
            " Whatever is in that cave is doing something else.\"",
        },
        /* line_count */ 6
    },
    {
        /* chapter */ "EASTWIND ARCHIVE — TYLER-089",
        /* title   */ "FIELD NOTE: 2026-06-16",
        /* lines   */ {
            "All five Goetia frequencies: ACTIVE.",
            "Andrealphus #72 (geometric transformation): PEAK.",
            "Stolas #36 (deep time sight): ELEVATED.",
            "",
            "The mechanism is attempting to measure.",
            "TEAM: FOLLOW. THE ARCHIVE CANNOT AFFORD A GAP.",
        },
        /* line_count */ 6
    },
};
const int g_cutscene_intro_count = 3;


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
        /* chapter */ "JIANGSHI MEMO #089 — DIRECTOR",
        /* title   */ "FOR INTERNAL DISTRIBUTION",
        /* lines   */ {
            "The breach generated zero farewell reading.",
            "The council will convene on a single question:",
            "",
            "What leaves nothing?",
            "",
            "The archive has no record. Tyler was there.",
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
};
const int g_cutscene_outro_count = 4;


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
