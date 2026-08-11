#include "typing_lesson.h"
#include <string.h>

/* VS0 acceptance criteria (NORTHSTAR_TYLER_TEACHES_TYPING.md): "Nothing
 * about the lesson content is invented; everything traces to an existing
 * script file." Every line below is copied verbatim from
 * TYLER/episodes/s01e01_pilot.md or s01e02_school.md — short, punchy
 * lines are picked on purpose (real typing-tutor line length), not
 * monologue paragraphs. */
const TypingSlide TYPING_SLIDES[] = {
    { "S01E01", "TYLER",     { "Depends who's asking." }, 1 },
    { "S01E01", "TYLER",     { "Is this the cut where you make me the villain?" }, 1 },
    { "S01E01", "TYLER",     { "You're early." }, 1 },
    { "S01E01", "SUBJECT #1", { "That's him." }, 1 },
    { "S01E01", "PRODUCER",  { "That's impossible." }, 1 },
    { "S01E01", "SUIT",      { "We can end this quietly." }, 1 },
    { "S01E01", "TYLER",     { "You've never ended anything." }, 1 },
    { "S01E01", "TYLER",     { "You only rename it." }, 1 },
    { "S01E01", "TYLER",     { "Shh." }, 1 },
    { "S01E01", "TYLER",     { "If you keep filming, you become part of it." }, 1 },
    { "S01E01", "TYLER",     { "But you're going to keep filming." }, 1 },
    { "S01E02", "CAMERA OP", { "How would he be in the student flow?" }, 1 },
    { "S01E02", "PRODUCER",  { "He is always in the flow. That's what the flow is for." }, 1 },
};
const int TYPING_SLIDE_COUNT = (int)(sizeof(TYPING_SLIDES) / sizeof(TYPING_SLIDES[0]));

#define TYPING_HOLD_MS 900 /* short pause before SPACE/ENTER is accepted to advance, mirrors CUTSCENE_HOLD_MS's purpose but shorter -- a completed line doesn't need 1.6s of forced reading */

static void typing_lesson_load_slide(TypingLessonState *ts, unsigned int now_ms) {
    const TypingSlide *s = &ts->slides[ts->current];
    ts->total_chars    = (s->line_count > 0 && s->lines[0]) ? (int)strlen(s->lines[0]) : 0;
    ts->cursor          = 0;
    ts->attempted_keys  = 0;
    ts->correct_keys    = 0;
    ts->miss_flash      = 0;
    ts->slide_start_ms  = now_ms;
    ts->first_key_ms    = 0;
    ts->slide_done       = (ts->total_chars == 0); /* an empty line counts as already done */
    ts->hold_until_ms    = now_ms;
    ts->last_wpm         = 0.0f;
    ts->last_accuracy    = 0.0f;
}

void typing_lesson_start(TypingLessonState *ts,
                          const TypingSlide *slides, int count,
                          unsigned int now_ms) {
    memset(ts, 0, sizeof(*ts));
    ts->slides      = slides;
    ts->slide_count = count;
    ts->current     = 0;
    if (count > 0) {
        typing_lesson_load_slide(ts, now_ms);
    } else {
        ts->done = 1;
    }
}

void typing_lesson_on_text(TypingLessonState *ts, const char *text, unsigned int now_ms) {
    if (ts->done || ts->slide_done || !text || text[0] == '\0') return;

    const TypingSlide *s = &ts->slides[ts->current];
    const char *line = s->lines[0];
    char typed = text[0];
    char expected = line[ts->cursor];

    if (ts->first_key_ms == 0) ts->first_key_ms = now_ms;
    ts->attempted_keys++;

    if (typed == expected) {
        ts->correct_keys++;
        ts->cursor++;
        ts->miss_flash = 0;
        if (ts->cursor >= ts->total_chars) {
            ts->slide_done      = 1;
            ts->hold_until_ms   = now_ms + TYPING_HOLD_MS;
            unsigned int elapsed_ms = now_ms - ts->first_key_ms;
            /* WPM = (correct chars / 5) / elapsed minutes -- standard net-WPM approximation
             * (5 chars = 1 "word"). Guard elapsed_ms == 0 (impossible in practice, SDL_GetTicks
             * resolution, but never divide by zero on a fast local test). */
            float minutes = elapsed_ms > 0 ? (float)elapsed_ms / 60000.0f : (1.0f / 60000.0f);
            ts->last_wpm      = ((float)ts->correct_keys / 5.0f) / minutes;
            ts->last_accuracy = ts->attempted_keys > 0
                ? (float)ts->correct_keys / (float)ts->attempted_keys
                : 1.0f;
        }
    } else {
        /* Wrong key: does not advance the cursor (house choice, flagged in the northstar --
         * "holds position" rather than skipping past the miss, so the player has to actually
         * retype the character they got wrong, same as most real typing tutors). */
        ts->miss_flash = 20; /* ~20 render frames of flash, cleared by the renderer's own decrement */
    }
}

void typing_lesson_advance(TypingLessonState *ts, unsigned int now_ms) {
    if (ts->done || !ts->slide_done || now_ms < ts->hold_until_ms) return;
    ts->current++;
    if (ts->current >= ts->slide_count) {
        ts->done = 1;
    } else {
        typing_lesson_load_slide(ts, now_ms);
    }
}
