/* anim_clipset.h -- the thirteen clips, loaded and selected.
 *
 * anim_state.h decides WHICH state a tick belongs to and publishes the clip-name contract; the
 * binder (tools/mixamo_clipset.py) produces a directory that satisfies it. This is the missing third
 * piece: a set of the thirteen real clips, loaded through GOLDENBAND's own loader, with the chosen
 * clip sampled at the right tick -- looping postures, holding one-shots, and falling back to a clip
 * that exists when the art does not.
 *
 * Header-only and free of GL/SDL, because the honest way to verify an animation selector is to load
 * real .gband files and ask which tick they are on, not to look at a window (and this build box has
 * no GPU or display at all).
 *
 * The fallback is the part that keeps a half-populated bundle usable instead of invisible: a rig
 * with no run clip plays its walk or its idle rather than freezing or drawing nothing. What it does
 * NOT do is invent motion -- the sampled pose is always a real clip's real tick.
 */
#ifndef SHANKPIT_ANIM_CLIPSET_H
#define SHANKPIT_ANIM_CLIPSET_H

#include <stdio.h>
#include <string.h>

#include "anim_state.h"
#include "../goldenband/gseq.h"
#include "../goldenband/gband.h"

typedef struct {
    GSeqClip clips[ANIM_STATE_COUNT];
    int loaded[ANIM_STATE_COUNT];
    /* state is what the rules asked for and a clip exists for; wanted is what the rules asked for,
     * whether or not it exists. Keeping both is what makes the fallback observable (and testable). */
    int state;
    int wanted;
    int prev_state;
    unsigned int tick;
    float blend_ms_remaining;
    float blend_ms_total;
    int one_shot_finished; /* a one-shot reached its last tick and is holding it */
} AnimClipSet;

static inline void anim_clipset_reset(AnimClipSet *set) {
    if (!set) return;
    memset(set, 0, sizeof *set);
    set->state = ANIM_IDLE;
    set->wanted = ANIM_IDLE;
    set->prev_state = ANIM_IDLE;
}

/* anim_clipset_load -- load <dir>/<contract>.gband + .gband.json for all thirteen names.
 *
 * Returns the number of clips that loaded. A missing or malformed clip is NOT fatal: it is recorded
 * as not loaded, so a rig with four clips still animates and a rig with none reports 0 rather than
 * pretending. gseq_clip_load already validates the binary and reads the channels from the manifest.
 */
static inline int anim_clipset_load(AnimClipSet *set, const char *dir) {
    if (!set || !dir) return 0;
    anim_clipset_reset(set);
    int count = 0;
    for (int s = 0; s < ANIM_STATE_COUNT; s++) {
        char gb[512], mj[512];
        const char *name = anim_state_clip_name(s);
        snprintf(gb, sizeof gb, "%s/%s.gband", dir, name);
        snprintf(mj, sizeof mj, "%s/%s.gband.json", dir, name);
        if (gseq_clip_load(gb, mj, &set->clips[s])) {
            set->loaded[s] = 1;
            count++;
        }
    }
    return count;
}

static inline int anim_clipset_all_loaded(const AnimClipSet *set) {
    if (!set) return 0;
    for (int s = 0; s < ANIM_STATE_COUNT; s++)
        if (!set->loaded[s]) return 0;
    return 1;
}

static inline void anim_clipset_free(AnimClipSet *set) {
    if (!set) return;
    for (int s = 0; s < ANIM_STATE_COUNT; s++) gseq_clip_free(&set->clips[s]);
    anim_clipset_reset(set);
}

/* The clip actually usable for `want`: the wanted state when its clip exists, else idle, else the
 * first clip that loaded. Returns -1 when nothing was loaded -- the caller should not draw at all.
 * Takes the wanted state as an argument rather than reading set->state, because the caller resolves
 * BEFORE committing (the first version read the previous state and so never left IDLE). */
static inline int anim_clipset_resolve_state(const AnimClipSet *set, int want) {
    if (!set) return -1;
    if (want >= 0 && want < ANIM_STATE_COUNT && set->loaded[want]) return want;
    if (set->loaded[ANIM_IDLE]) return ANIM_IDLE;
    for (int s = 0; s < ANIM_STATE_COUNT; s++)
        if (set->loaded[s]) return s;
    return -1;
}

static inline float anim_clipset_blend_weight(const AnimClipSet *set) {
    if (!set || set->blend_ms_total <= 0.0f) return 1.0f;
    float w = 1.0f - (set->blend_ms_remaining / set->blend_ms_total);
    return w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w);
}

/* anim_clipset_tick -- advance `dt_ms` and return the state to sample, or -1 if no clip is loaded.
 *
 * On a state change the new clip starts at tick 0 and a crossfade begins for the rules' own
 * anim_state_blend_ms. Looping states wrap by duration; one-shots hold their last tick, because a
 * reload that snaps back to frame 0 reads as a glitch rather than a completed action.
 */
static inline int anim_clipset_tick(AnimClipSet *set, const PlayerState *p, int slide_ticks, int climb, int dt_ms) {
    if (!set || dt_ms < 0) return -1;
    set->wanted = anim_state_for_player(p, slide_ticks, climb);
    int resolved = anim_clipset_resolve_state(set, set->wanted);
    if (resolved < 0) return -1;

    if (resolved != set->state) {
        set->prev_state = set->state;
        set->state = resolved;
        set->tick = 0;
        set->one_shot_finished = 0;
        set->blend_ms_total = (float)anim_state_blend_ms(resolved);
        set->blend_ms_remaining = set->blend_ms_total;
    }

    const GBClip *clip = &set->clips[set->state].clip;
    unsigned int advance = (unsigned int)(((long long)dt_ms * (long long)clip->tick_rate) / 1000);
    int loops = anim_state_loops(set->state);
    if (loops) {
        set->tick = (set->tick + advance) % clip->duration_ticks;
    } else {
        unsigned long long t = (unsigned long long)set->tick + advance;
        if (t >= clip->duration_ticks) {
            set->tick = clip->duration_ticks - 1;
            set->one_shot_finished = 1;
        } else {
            set->tick = (unsigned int)t;
        }
    }

    if (set->blend_ms_remaining > 0.0f) {
        set->blend_ms_remaining -= (float)dt_ms;
        if (set->blend_ms_remaining < 0.0f) set->blend_ms_remaining = 0.0f;
    }
    return set->state;
}

/* The channel values for the current tick (num_channels floats, owned by the set), or NULL. */
static inline const float *anim_clipset_sample(const AnimClipSet *set) {
    if (!set) return NULL;
    if (!set->loaded[set->state]) return NULL;
    return gb_sample(&set->clips[set->state].clip, set->tick);
}

/* The active clip itself, for a caller that needs duration/tick_rate or wants to blend. */
static inline const GBClip *anim_clipset_clip(const AnimClipSet *set) {
    if (!set || !set->loaded[set->state]) return NULL;
    return &set->clips[set->state].clip;
}

#endif /* SHANKPIT_ANIM_CLIPSET_H */
