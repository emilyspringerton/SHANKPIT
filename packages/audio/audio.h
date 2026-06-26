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

#endif /* AUDIO_H */
