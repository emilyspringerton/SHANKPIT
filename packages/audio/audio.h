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
 * (snare + crash cymbal), 2 = shield pack pickup chime. Same listener conventions as
 * audio_play_weapon. */
void audio_play_shield(int kind, float sx, float sy, float sz,
                       float lx, float ly, float lz, float lyaw);

/* A shield regen tick, pitched by its ramp step: 0 is the first tick (worth 1 point), 9 the tenth and
 * every later one (10 apiece). One short, quiet blip per second while a broken shield comes back; the
 * pitch climbs with the ramp so recovery is heard as getting stronger, and the amplitude does not, so
 * nineteen seconds of ticks never get louder than the fight. */
void audio_play_shield_regen(int step, float sx, float sy, float sz,
                             float lx, float ly, float lz, float lyaw);

/* The civic boss's verdict (SHANKPIT's AI_ROLE_MAGISTRATE), synthesized into the same bank as every
 * other cue: kind 0 = the sentence being READ (a short wooden gavel rap, the telegraph's own sound),
 * kind 1 = the sentence SERVED (a heavy stamp plus a sub drop). Positioned exactly like a gunshot --
 * source at the bench, listener position + yaw -> the same sin(rel_angle) pan and 1/(1+dist^2/1e4)
 * attenuation every other sound in this engine uses, so walking down the gallery really does move the
 * verdict to one side of your head and fade it. */
void audio_play_magistrate(int kind, float sx, float sy, float sz,
                           float lx, float ly, float lz, float lyaw);

/* The wind bed (founder: "add basic wind sound effects for storms"). A looping, filtered noise buffer
 * mixed under every effect. gain/bright/pan are already-resolved 0..1 (pan -1..1) values from
 * PARENA's weather rules -- audio.c does the DSP, not the weather, so the storm that sounds like a gale
 * is the same storm that thins the ground fog. gain 0 fades it out; audio_init starts it still and
 * silent until a host calls this. Called every frame; targets are smoothed, so a jump in the sim cannot
 * click in the mix. */
void audio_set_wind(float gain, float bright, float pan);

/* Thunder. A strike is SEEN first and HEARD later: delay_ms is the travel time PARENA's weather rules
 * resolved from the strike's real distance, so the host needs no clock arithmetic of its own. The rumble
 * is held in a fixed four-deep queue that the AUDIO CALLBACK counts down in frames, so it starts on the
 * exact buffer its travel time expires in rather than on the next render frame. */
void audio_play_thunder(int delay_ms, float gain, float pan);

/* The one siren voice. kind is a SIREN_KIND_* from PARENA's siren rules (0 = silent), hz_milli the
 * frequency in millihertz those rules resolved for this frame, gain 0..1 and pan -1..1. A host with no
 * sirens never calls it and hears exactly what it heard before. */
void audio_set_siren(int kind, int hz_milli, float gain, float pan);
/* The vendor's jingle: playing = PARENA icecream-trigger, gain/pan resolved at the listener, and jingle
 * the truck's OWN melody (icecream-jingle-for-id) so two trucks on one street are two tunes to the ear
 * rather than one tune in two keys. Jingle 0 is the authored song. */
void audio_set_icecream(int playing, float gain, float pan, int jingle);
/* the chime a STOPPED vendor rings instead (the same rule inverted; never both at once) */
void audio_set_icecream_chime(int playing, float gain, float pan);

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
