# TYLER — Flashback: "The First Time They Play House"

**Mode:** MODE_TYLER (engine-driven, same coordinator pattern as VH01 / Episode 3).
**Language:** French dialogue, bracketed English gloss — same `TYLER_SUBTITLE_*` convention.
**Placement choice:** engine flashback, not an ARPANET archive node — this scene is present-tense,
two-hander dialogue with no "Eastwind Owl / Cataloguer" documentation framing, so it belongs in
`tyler_fb01_coldopen.{h,c}` (SHANKPIT engine) rather than `BIG_O`'s `BP_ARPANET_BODIES` table.
**Status:** Script + 8-beat blocking table authored this session. No rendered audio (same named
gap as Episode 3 — Piper/`TYLER` repo not present in this sandbox); subtitles still render from
the beat table itself. Not build/run-verified (token-pressure oneshot, same as Episode 3).

## Cast (engine-mapped)

Two in-engine actors only: `TYLER_ACTOR_TYLER` and `TYLER_ACTOR_HANA`, Hana's apartment interior.

## Beat table (engine blocking)

| Beat | Actor | Hold | Subtitle (FR / [EN]) |
|---|---|---|---|
| 0 | BOTH | 3000ms | On pourrait... juste rester. Comme des gens normaux. [EN: We could just stay in. Like normal people.] |
| 1 | HANA | 2500ms | Les gens normaux ne disent pas ca a voix haute. [EN: Normal people don't say that sentence out loud.] |
| 2 | BOTH | 3000ms | Qu'est-ce que tu as fait. [EN: What did you do.] |
| 3 | HANA | 2000ms | Chaussures. Maintenant. [EN: Shoes. Now.] |
| 4 | TYLER | 3000ms | Tout ceci... c'est une illusion, tu sais. On est en route pour Mars. [EN: This is all an illusion, you know. We're on our way to Mars.] |
| 5 | TYLER | 3000ms | Tu es Persephone. Tu es... ma soeur. [EN: You're Persephone. You're... my sister.] |
| 6 | HANA | 2500ms | Tu as pisse dans mon lit. [EN: You pissed in my bed.] |
| 7 | TYLER | 2500ms | Ca va tout avoir un sens quand on se reveillera. [EN: It's all going to make sense when we wake up.] *(fires exit)* |

Level: `assets/tyler_levels/tyler_fb01_apartment.json`, a small interior (couch, bed, door) — new
markers, not shared with VH01/Episode 3's own geometry.

## Known gaps (named, not hidden)

Identical shape to Episode 3's own gaps doc: no rendered VO (`g_tyler_fb01_voice_line_count = 0`,
Piper/`TYLER` repo absent); coordinator compiles standalone but isn't wired into `apps/server/src/
main.c`'s `--tyler` flag path; not build/run-verified this session.
