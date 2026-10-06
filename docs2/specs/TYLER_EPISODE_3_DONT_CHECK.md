# TYLER — Episode 3: "Don't Check" (ÉPISODE 3 : « NE VÉRIFIE PAS »)

**Mode:** MODE_TYLER (engine-driven, same coordinator pattern as VH01 cold open)
**Language:** French dialogue (spoken/subtitled), bracketed English gloss — same convention as
`episodes/vh01_valhanna_coldopen.md` / `TYLER_SUBTITLE_*`.
**Status:** Script + full engine blocking table authored this session. **No rendered audio** —
Piper and the `TYLER` repo (`tts/render_vh01.py`, the beats TSV, the `.onnx` voice models) are not
present in this sandbox, so all 8 beats ship as **silent** (`g_tyler_e03_voice_line_count = 0`),
same labeled-stopgap honesty as VH01's own beat 6. Subtitles still render (they come from the beat
table directly, not from the voice lines table), so the scene is fully playable/blockable now;
real VO drops in later exactly the way VH01's did.

---

## Cast (engine-mapped)

The documentary crew (Producer, Editor, Cameraman, Contractor, Archivist) are **off-screen / V.O.
framing only** — there is no engine actor for them, matching how VH01 never modeled a camera crew
either. The two in-engine actors are **Tyler** and **Hana** (`TYLER_ACTOR_TYLER` / `TYLER_ACTOR_HANA`
/ `TYLER_ACTOR_BOTH`), same two player slots `story_ai_spawn_enemy` already returns for this
level's authored characters.

---

## Beat table (engine blocking)

| Beat | Actor | Marker (x,y,z) | Hold | Subtitle (FR / [EN]) |
|---|---|---|---|---|
| 0 | BOTH | (0, 1, 6) | 3000ms | Petit. C'est comme ça que ça se répand. / Ne vérifie rien aujourd'hui. [EN: Small is how it spreads. / Don't check anything today.] |
| 1 | HANA | (2, 1, 5) | 2500ms | La porte à laquelle tu penses. [EN: The door you're thinking about.] |
| 2 | TYLER | (4, 1, 2) | 2500ms | Rien n'est "juste" une histoire, maintenant. [EN: Nothing is "just" a story anymore.] |
| 3 | BOTH | (6, 1, -2) | 3000ms | Reste droit devant. Se retourner est plus dangereux. [EN: Stay looking straight ahead. Turning is more dangerous.] |
| 4 | TYLER | (6, 1, -5) | 3000ms | Je veux l'ouvrir. L'univers ne peut pas me commander. [EN: I want to open it. The universe can't boss me around.] |
| 5 | BOTH | (6, 1, -8) | 2500ms | ARCHIVISTE. Ce n'était pas dans les recherches. [EN: ARCHIVIST. That wasn't in the background research.] |
| 6 | NONE (silent action) | — | 2000ms | *(Une main gantée prend la mallette. Aucun visage.)* [EN: (A gloved hand takes the case. No face.)] |
| 7 | TYLER | (6, 1, -9) | 2000ms | Ne vérifie pas. [EN: Don't check.] *(fires exit)* |

Marker positions are placeholder-authored for this episode's own alley/Archivist-door level (see
`assets/tyler_levels/tyler_e03_dont_check.json`), the same way VH01's own markers matched its
level's props exactly (printer, ecs_screen, exit button).

---

## Prose script (for reference / future VO line-reading)

INT./EXT. condensed from the founder-supplied treatment — surveillance van framing kept as narration
context, not engine-modeled:

- Dawn. Tyler outside his apartment, "audited" look. Hana arrives, no knock: *"Petit, c'est comme
  ça que ça se répand... ne vérifie rien aujourd'hui... la porte à laquelle tu penses."*
- The Contractor beat (van-window knock, "DO NOT CHECK" card) is V.O.-only color; not an engine
  beat — no in-engine Contractor actor exists.
- Street courier walk (beats 2–4): Tyler carries the black case, wants to open it.
- Archivist door (beat 5–6): gloved hand takes the case, no face shown. Silent beat, matching VH01
  beat 6's own "fed the printout back unread" silent-action convention.
- Beat 7: the glitch line — "Ne vérifie pas." — doubles as the in-fiction creepy insert AND the
  real engine's `fires_button` exit beat, same mechanism VH01 used (`story_force_level_transition`
  called directly, no LevelExit trigger volume).

---

## Known gaps (named, not hidden)

- **No rendered VO.** `TYLER/tts/render_vh01.py`, the beats TSV, and the Piper `fr_FR-tom-medium`/
  `fr_FR-siwis-medium` models live in the `TYLER` repo, which is not checked out in this sandbox.
  `g_tyler_e03_voice_lines[]` is empty; HUD subtitles still work (they're compiled into the beat
  table itself, independent of the voice lines table — same separation VH01 already has).
- **Server wiring is coordinator-only.** `tyler_e03_coldopen.{h,c}` mirrors `tyler_coldopen.{h,c}`
  exactly (start/tick/get_view/voice-driver) and compiles standalone, but is not yet hooked into
  `apps/server/src/main.c`'s `--tyler` flag handling or a new level file's character spawn pass —
  same P0 integration gap the VH01 investigation named for the *original* cold open
  (`TYLER_VOICE_INTEGRATION_PLAN.md`), not re-solved here under token pressure this session.
- **Level JSON is new, unverified in-engine.** Not loaded/boot-tested against the live lobby/server
  this session (no build/run pass was done — explicit per founder direction this turn to skip
  verification given token constraints). Treat marker coordinates as design-only until a real load
  test confirms collision/ground-plane fit.
