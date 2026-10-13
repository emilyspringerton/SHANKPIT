#ifndef AUDIO_H
#define AUDIO_H

/* Spatial audio engine for SHANKPIT.
 *
 * All sounds are synthesized at init time as PCM wavetables — no external
 * files needed.  Weapon sounds use MIDI drum & bass archetypes:
 *   knife/katana  = metallic hi-hat
 *   magnum        = 808 kick (low sweep)
 *   AR            = tight hi-hat burst
 *   shotgun       = layered kick + noise
 *   sniper        = sub-bass boom
 *
 * Footsteps cycle through a C-major pentatonic scale (C4 D4 E4 G4 A4).
 *
 * Spatial panning: listener position + yaw → left/right gain via sin(rel_angle).
 * Distance attenuation: soft rolloff, ~full volume inside 80 units.
 */

void audio_init(void);
void audio_shutdown(void);

/* Play a weapon fire sound at world position (sx,sy,sz).
 * lx,ly,lz = listener world position; lyaw = listener yaw in radians. */
void audio_play_weapon(int weapon_idx,
                       float sx, float sy, float sz,
                       float lx, float ly, float lz,
                       float lyaw);

/* Play a melodic footstep note at the given world position.
 * step_index cycles through the pentatonic scale. */
void audio_play_footstep(float sx, float sy, float sz,
                         float lx, float ly, float lz,
                         float lyaw, int step_index);

/* Shield sounds, synthesized in the same bank (cards #539-541): kind 0 = shield hit zap, 1 = shield break
 * (snare + crash cymbal), 2 = shield pack pickup chime. Same listener conventions as audio_play_weapon. */
void audio_play_shield(int kind, float sx, float sy, float sz,
                       float lx, float ly, float lz, float lyaw);

/* NOCK master chain (2026-09-27): run a filter chain authored in IDUNA's NOCK "Sounds" tab over
 * the whole engine mix. audio_init() loads SHANKPIT_SOUND_CHAIN=<name> automatically from
 * SHANKPIT_SOUND_CHAIN_URL (default https://okemily.com/api/v1/nock-sound-filters). Returns 1
 * if the chain was fetched and is now live; passing NULL removes the chain. See audio_chain.h. */
int audio_set_master_chain(const char *name);

/* Spoken clips (MODE_TYLER VO, generated at build time -- see audio_wav.h). audio_clip_load decodes a
 * WAV once and caches it by path (returns a clip id, or -1). audio_clip_play starts it `seek_ms` in,
 * positioned like any other sound; clips use their own small voice pool so a footstep or gunshot can
 * never cut a line. audio_clip_stop_all cuts every playing clip (scene change / lost sync). */
int  audio_clip_load(const char *wav_path);
void audio_clip_play(int clip_id, unsigned int seek_ms,
                     float sx, float sy, float sz,
                     float lx, float ly, float lz, float lyaw);
void audio_clip_stop_all(void);

#endif /* AUDIO_H */
