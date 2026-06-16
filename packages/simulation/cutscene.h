/*
 * cutscene.h — TYLER × SHANKPIT cutscene system
 *
 * Typewriter slide sequencer. Client-side only — no network state.
 * Slides hold TYLER episode lore bridging the horror gameplay.
 *
 * Phase contract:
 *   STORY_PHASE_CUTSCENE — intro (Tyler arrives at the breach site)
 *   STORY_PHASE_OUTRO    — outro (Tyler files post-breach observation)
 *
 * Usage:
 *   cutscene_start(&g_cs, g_cutscene_intro, g_cutscene_intro_count, SDL_GetTicks());
 *   each frame: cutscene_tick(&g_cs, SDL_GetTicks());
 *   on key:     cutscene_advance(&g_cs, SDL_GetTicks());
 *   when done:  g_cs.done == 1 → transition phase
 */

#ifndef CUTSCENE_H
#define CUTSCENE_H

#define CUTSCENE_MAX_SLIDES     6
#define CUTSCENE_SLIDE_LINES    6
#define CUTSCENE_LINE_LEN       120
#define CUTSCENE_TYPEWRITER_MS  26   /* ms per character revealed */
#define CUTSCENE_HOLD_MS        1600 /* ms to hold after text finishes before auto-advance */

typedef struct {
    const char *chapter;  /* small tag: "CHAPTER I"    — NULL to omit */
    const char *title;    /* large title line           — NULL to omit */
    const char *lines[CUTSCENE_SLIDE_LINES];
    int         line_count;
} CutsceneSlide;

typedef struct {
    const CutsceneSlide *slides;
    int                  slide_count;

    int          current;          /* index into slides[]              */
    unsigned int slide_start_ms;   /* when current slide began         */
    int          chars_revealed;   /* typewriter position in flat text */
    unsigned int last_char_ms;     /* when last char was revealed      */
    int          text_done;        /* typewriter finished on this slide*/
    unsigned int hold_until_ms;    /* auto-advance not before this     */
    int          done;             /* all slides exhausted             */
} CutsceneState;

/* Start a cutscene from the first slide. */
void cutscene_start(CutsceneState *cs,
                    const CutsceneSlide *slides, int count,
                    unsigned int now_ms);

/* Advance typewriter and auto-advance slides. Call every frame. */
void cutscene_tick(CutsceneState *cs, unsigned int now_ms);

/* Player pressed advance key — skip typewriter or advance slide. */
void cutscene_advance(CutsceneState *cs, unsigned int now_ms);

/* Total character count for current slide's body lines (for typewriter). */
int cutscene_slide_total_chars(const CutsceneSlide *s);

/* ── predefined slide sets ─────────────────────────────────────────── */

extern const CutsceneSlide g_cutscene_intro[];
extern const int           g_cutscene_intro_count;

extern const CutsceneSlide g_cutscene_outro[];
extern const int           g_cutscene_outro_count;

#endif /* CUTSCENE_H */
