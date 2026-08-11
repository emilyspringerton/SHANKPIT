/*
 * typing_lesson.h — Tyler Teaches Typing, VS0
 *
 * A real typing tutor: the player types a line of TYLER's own dialogue,
 * character by character, and gets real WPM/accuracy back. Sibling to
 * cutscene.h (same package, same era), reusing its visual language
 * (packages/simulation/cutscene.h's doc comment / apps/lobby's
 * draw_tyler_cutscene) but NOT its struct — cutscene.c's chars_revealed
 * is purely timer-driven; this is keystroke-driven, a real gap that
 * didn't exist anywhere in this codebase before VS0 (see
 * docs2/NORTHSTAR_TYLER_TEACHES_TYPING.md's own "Real Technical Gap"
 * section).
 *
 * VS0 scope: one line of real dialogue per slide (TYPING_SLIDE_LINES
 * exists for shape-parity with CutsceneSlide/future multi-line lessons,
 * but every VS0_SLIDES entry sets line_count = 1 — no multi-line cursor
 * mapping needed yet, real scope discipline, not an oversight).
 *
 * Usage:
 *   typing_lesson_start(&g_typing, TYPING_SLIDES, TYPING_SLIDE_COUNT, SDL_GetTicks());
 *   on SDL_TEXTINPUT: typing_lesson_on_text(&g_typing, e.text.text, SDL_GetTicks());
 *   on SPACE/ENTER after a slide is done: typing_lesson_advance(&g_typing, SDL_GetTicks());
 *   when g_typing.done == 1: return to lobby.
 */

#ifndef TYPING_LESSON_H
#define TYPING_LESSON_H

#define TYPING_MAX_SLIDES   16
#define TYPING_SLIDE_LINES  1    /* VS0: one line per slide, see header doc above */
#define TYPING_LINE_LEN     160

typedef struct {
    const char *chapter;  /* episode tag, e.g. "S01E01" — NULL to omit */
    const char *title;    /* speaker name, e.g. "TYLER" — NULL to omit */
    const char *lines[TYPING_SLIDE_LINES];
    int         line_count;
} TypingSlide;

typedef struct {
    const TypingSlide *slides;
    int          slide_count;
    int          current;            /* index into slides[] */

    int          total_chars;        /* strlen of current slide's line */
    int          cursor;             /* how many chars correctly advanced past */
    int          attempted_keys;     /* every keypress counted, right or wrong (for accuracy) */
    int          correct_keys;       /* only the keypresses that advanced the cursor */
    int          miss_flash;         /* >0: just missed a char, hold a visual flash this many ticks */

    unsigned int slide_start_ms;     /* first keypress on this slide (WPM clock start) */
    unsigned int first_key_ms;       /* 0 until the first real keypress on this slide */
    int          slide_done;         /* cursor reached total_chars */
    unsigned int hold_until_ms;      /* SPACE/ENTER not accepted to advance before this */
    int          done;               /* all slides exhausted */

    float        last_wpm;           /* computed once, when slide_done flips on */
    float        last_accuracy;      /* correct_keys / attempted_keys, 0..1 */
} TypingLessonState;

/* Start (or restart) a lesson from its first slide. */
void typing_lesson_start(TypingLessonState *ts,
                          const TypingSlide *slides, int count,
                          unsigned int now_ms);

/* Feed one real SDL_TEXTINPUT event's text (usually 1 char, but SDL can
 * hand back multi-byte IME input — only the first byte is used, VS0 is
 * ASCII dialogue only). No-op if the slide is already done or the lesson
 * is done. */
void typing_lesson_on_text(TypingLessonState *ts, const char *text, unsigned int now_ms);

/* Player pressed SPACE/ENTER after finishing a slide — advance to the
 * next one, or set done=1 if that was the last slide. No-op if the
 * current slide isn't done yet or the hold window hasn't elapsed. */
void typing_lesson_advance(TypingLessonState *ts, unsigned int now_ms);

/* ── VS0 slide table — real TYLER dialogue, unmodified ───────────────── */
extern const TypingSlide TYPING_SLIDES[];
extern const int         TYPING_SLIDE_COUNT;

#endif /* TYPING_LESSON_H */
