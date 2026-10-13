#ifndef AUDIO_WAV_H
#define AUDIO_WAV_H
#include <stdint.h>

/* SDL-free WAV decode for spoken clips (MODE_TYLER VO). Accepts canonical RIFF/WAVE, PCM16, mono or
 * stereo, at exactly the engine rate (22050 Hz -- TYLER/tts/render_scenes.py guarantees it).
 * On success returns the frame count (> 0) and *out_stereo = malloc'd interleaved stereo (mono is
 * duplicated); the caller frees it. Returns 0 on any failure (missing file, other format/rate). */
int audio_wav_load_stereo16(const char *path, int16_t **out_stereo);

/* Start frame for a late join `seek_ms` into a clip, clamped to [0, frames]. */
int audio_wav_seek_frame(unsigned int seek_ms, int frames);

#define AUDIO_WAV_RATE 22050

#endif
