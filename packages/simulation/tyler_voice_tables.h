/* tyler_voice_tables.h -- per-scene voice line tables + per-beat clip lengths.
 *
 * The real tables are GENERATED AT BUILD TIME by TYLER/tts/render_scenes.py into gen/tyler_voice_<scene>.h
 * (Piper, a labeled stopgap; CI only -- nothing generated is committed, per founder 2026-10-13). When
 * a scene's generated header is absent (a local build without Piper) that scene falls back to an
 * empty table: silent, subtitles unaffected (they are committed in each scene's beat table).
 * Needs tyler_coldopen.h included first (TylerVoiceLine, TYLER_ACTOR_*).
 */
#ifndef SHANKPIT_TYLER_VOICE_TABLES_H
#define SHANKPIT_TYLER_VOICE_TABLES_H

/* Each translation unit sees all four scenes' tables; the unused ones are expected. */
#if defined(__GNUC__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wunused-const-variable"
#endif

#if defined(__has_include)
# if __has_include("gen/tyler_voice_vh01.h")
#  include "gen/tyler_voice_vh01.h"
#  define TYLER_VH01_VOICE_GENERATED 1
# endif
#endif
#ifndef TYLER_VH01_VOICE_GENERATED
static const unsigned int g_tyler_vh01_voice_clip_ms[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
# define TYLER_VH01_VOICE_LINE_COUNT 0
# define TYLER_VH01_VOICE_LINES_INIT { { 0, TYLER_ACTOR_NONE, 0, 0, "" } }
#endif

#if defined(__has_include)
# if __has_include("gen/tyler_voice_e03.h")
#  include "gen/tyler_voice_e03.h"
#  define TYLER_E03_VOICE_GENERATED 1
# endif
#endif
#ifndef TYLER_E03_VOICE_GENERATED
static const unsigned int g_tyler_e03_voice_clip_ms[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
# define TYLER_E03_VOICE_LINE_COUNT 0
# define TYLER_E03_VOICE_LINES_INIT { { 0, TYLER_ACTOR_NONE, 0, 0, "" } }
#endif

#if defined(__has_include)
# if __has_include("gen/tyler_voice_fb01.h")
#  include "gen/tyler_voice_fb01.h"
#  define TYLER_FB01_VOICE_GENERATED 1
# endif
#endif
#ifndef TYLER_FB01_VOICE_GENERATED
static const unsigned int g_tyler_fb01_voice_clip_ms[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
# define TYLER_FB01_VOICE_LINE_COUNT 0
# define TYLER_FB01_VOICE_LINES_INIT { { 0, TYLER_ACTOR_NONE, 0, 0, "" } }
#endif

#if defined(__has_include)
# if __has_include("gen/tyler_voice_vh02.h")
#  include "gen/tyler_voice_vh02.h"
#  define TYLER_VH02_VOICE_GENERATED 1
# endif
#endif
#ifndef TYLER_VH02_VOICE_GENERATED
static const unsigned int g_tyler_vh02_voice_clip_ms[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
# define TYLER_VH02_VOICE_LINE_COUNT 0
# define TYLER_VH02_VOICE_LINES_INIT { { 0, TYLER_ACTOR_NONE, 0, 0, "" } }
#endif

#if defined(__GNUC__)
# pragma GCC diagnostic pop
#endif

#endif /* SHANKPIT_TYLER_VOICE_TABLES_H */
