# TYLER × SHANKPIT Horror Mechanics Spec
## Build 0110 | Inspired by TYLER S10E07–S10E08 lore

---

## What This Is

This spec documents the three horror mechanics added in Build 0110, all derived directly from
TYLER × TIDES OF PARADOX lore. The design principle: TYLER's world is bureaucratic horror and
archival dread — things that cannot be classified, procedures that cannot close, questions that
outlast the people who asked them. SHANKPIT story mode translates this into moment-to-moment
gameplay.

---

## 1. Mechanism Reader HUD

**TYLER lore origin:** Tyler's archive mechanism measures Goetia Hz frequencies at sites. The
Stolas baseline is 7.83 Hz. Different site classes produce different frequency ranges. The cave
(CAVE-001 / al-Waqfa) produces a zero reading — the mechanism finds its own source and cannot
classify what it identifies as itself.

**In-game:**
- Persistent HUD element (bottom-left, 8px × 78px panel) during story mode
- Shows current Hz reading (e.g., `7.83 Hz`) and derived site taxonomy class
- Hz oscillates around Stolas baseline; spikes toward LIMINAL/FAREWELL as boss health drops
- Each active swarm enemy adds +0.3 Hz
- Taxonomy classes: UNCLASSIFIED → PASSAGE → WITNESS → DWELLING → LIMINAL → FAREWELL
- Archive pressure bar shows observation intensity (0..1)

**Horror moment:** When question-state activates, the HUD collapses: `NO READING / ZERO TAXONOMY`
displayed in flickering red. The site has become al-Waqfa — the mechanism finds its own source.
Text: `ZERO TAXONOMY — PROCEED AT OWN RISK`.

**Implementation:** `draw_mechanism_hud()` in `apps/lobby/src/main.c`, `mechanism_tick()` +
`MechanismReading` struct in `packages/simulation/local_game.h` and `packages/common/protocol.h`.

---

## 2. Boss Question-State Phase

**TYLER lore origin:** MARRAKECH-001 carries a question-state signal — Ahmad ibn Yusuf's unresolved
question, persisting for nine centuries. The signal cannot decay because the answer hasn't been
given. Tyler: "The archive and the question haven't met yet." The question-state resolves only when
someone reads both archive and blank and speaks for both — not finding a word, but closing the
paragraph.

**In-game trigger:** Boss health reaches 33% (`QUESTION_STATE_HP_THRESHOLD = 0.33f`)

**Phase behavior:**
- Boss takes only 10% normal damage (damage scale `0.10f`)
- Mechanism reading collapses to zero (zero-taxonomy zone)
- Boss HUD shows `TAXONOMY: ????????` in pulsing grey-violet
- Objective text: `MECHANISM: ZERO READING / FILE OBSERVATION — APPROACH THE BREACH SITE`
- Flavour tag: `AL-WAQFA  //  THE PAUSE`

**Resolution:** Player approaches the observation point (glowing green ring, 28-unit radius,
60 units in front of boss). After 4 seconds minimum (`QUESTION_MIN_ACTIVE_MS`), reaching the
point within `QUESTION_OBS_RADIUS = 32.0f` resolves the question-state.

**Visual:** Question-state aura on boss:
- Drifting grey-violet ring (48-segment polyline with wobble)
- 20-particle blinking cloud of grey boxes — archive dissolving
- Observation point ground marker (green pulsing ring on terrain)

**Resolution effect:** Mechanism snaps back to Stolas baseline; boss fully vulnerable; archive
pressure climbs back toward 1.0.

**Implementation:** `story_boss_question_state_tick()`, `story_boss_apply_player_hit()` (10%
damage gate), `draw_story_boss_world()` question-state aura, `draw_story_boss_hud()` grey bar
state, `draw_mechanism_hud()` zero-reading display.

---

## 3. Cutscene Slide Extensions (S10 Arc)

**New intro slides (Build 0110, slides 6–8 of 8):**
- Slide 6: MARRAKECH-001 three-signal site classification (ZERO-TAXONOMY / EXECUTION-SITE /
  ARCHIVE-ORIGIN / QUESTION-SITE). Three signals: Stolas 7.83, archive-state, question-state.
- Slide 7: al-Waqfa oral addendum. "I gave it to a scholar on a ship four hours before port.
  He wrote it in the margin of his gap. Al-Waqfa. The standing. The cave. Everything else is
  that word, expanded."
- Slide 8: Jiangshi Memo #094 — al-idrak al-muttasil (continuous perception). "The breach site
  is the second instance. You are the observer it is recording now." — reframes the player as
  a practitioner of the method.

**New outro slides (Build 0110, slides 7–8 of 8):**
- Slide 7: Jiangshi Memo #094-B post-breach. Question-state resolved. ACR-089 closed.
  "Ahmad ibn Yusuf's gap: filed."
- Slide 8: Camera Op Entry 88. Tyler: "It knows what this site is now. Something stayed.
  Something was recorded." The mechanism doesn't look for classification anymore — it found one.

**Lore arc:** The intro establishes the site's nine-century history and tells the player they
are being observed. The outro confirms the observation landed — the site classified itself
through the player's presence.

---

## Signal Table (Mechanism Reader)

| Hz range | Taxonomy class | Game state |
|---|---|---|
| 0.0 | ZERO TAXONOMY | Question-state / cave phenomenon |
| 2.0–6.0 | PASSAGE | Low-intensity, no boss |
| 6.0–8.5 | WITNESS | Near Stolas baseline — site is being observed |
| 8.5–12.0 | DWELLING | Boss mid-health, some enemies |
| 12.0–18.0 | LIMINAL | Boss low-health, swarm active |
| 18.0+ | FAREWELL | Rift open, full emergency |

---

## Boss Phase Summary

| Phase | Trigger | Boss damage | Mechanism | Player objective |
|---|---|---|---|---|
| Normal | Start | 100% | Live reading | Defeat boss |
| Question-state | 33% health | 10% | Zero reading | File observation at marker |
| Resolved | Reach marker | 100% | Snaps to Stolas | Resume defeating boss |
| Post-boss | Boss defeated | — | FAREWELL / LIMINAL | Survive swarm |

---

*TYLER S10E07 — "The Third Day": al-idrak al-muttasil named. MARRAKECH-001 question-state confirmed.*
*TYLER S10E08 — "Fez, December 1119": al-Waqfa recovered. First word = last word = cave name.*
*SHANKPIT Build 0110 — these lore beats encoded as playable horror mechanics.*

---

## 4. CAVE-001 Second Mission — al-Waqfa Endurance Level

**Build 0111 | TYLER S10E02–E03 | Apple #1074**

**TYLER lore origin:** CAVE-001 — the pre-archive Tyler's presence. The mechanism reads the
cave at Stolas 7.83 Hz and returns nothing because 7.83 is the mechanism's own operating
baseline. The cave IS the source. "You cannot read yourself. You cannot observe the condition
that makes observation possible." Tyler was here at age twelve — before the archive, before
categories, before the mechanism existed. Whatever he left here predates taxonomy.

**Gameplay:** `MODE_STORY_CAVE` — LOBBY button "CAVE-001."

**The entity (indestructible):**
- `StoryBossState.indestructible = 1` — shots register zero damage
- No health bar HUD — "ARCHIVE SOURCE // ZERO-TAXONOMY" / "CANNOT BE NAMED. CANNOT BE CLOSED."
- Visual: `draw_story_cave_entity()` — three slow near-grey ring loops at different orbital speeds,
  16 particles orbiting at Stolas 7.83 rhythm, ground ring breathing at 7.83 Hz. No body geometry.
  Not a creature — a presence that drifts through the space at the mechanism's own frequency.

**Win condition:** Endure 90 seconds (`CAVE_ENDURE_DURATION_MS`). No defeat. No closure.
HUD shows countdown: "ENDURE 89" → "ENDURE 1" → outro plays.

**Mechanism:** Always `ZERO_TAXONOMY`, `signal_hz ≈ 0.0f`. The HUD flickers red static. No
question-state, no observation point, no resolution. The zero reading is permanent.

**Cutscene (intro, 8 slides):**
- CAVE-001 at Stolas 7.83 — mechanism recognizes its own frequency
- ACR-089-CAVE: taxonomy cannot be assigned, form stays open
- Jiangshi Tier One: cave may be the source of the mechanism's calibration baseline
- Camera Op Entry 81: Tyler went in. He said: I was here before.
- Mechanism log: 16 hours — something is observing my entry
- Mechanism log Day 2 04:00: PRE-VASSAGO 11.4; the number 12
- Tyler: "I was here when I was twelve. The cave holds what I had before the first framework."
- Jiangshi Memo: "You cannot read yourself. You are inside it now. It is recording."

**Cutscene (outro, 6 slides):**
- Tyler endured 48 hours. He did not defeat anything.
- Mechanism returned: 12. Not a frequency. Not a classification. Just: 12.
- ACR-089-CAVE: STILL OPEN. Cannot close. No taxonomy for source material.
- Jiangshi: The cave is not a problem to solve. It is the condition the problem comes from.
- Camera Op: "He said: it is what it is. The cave holds."
- Tyler Archive: "CAVE-001: al-Waqfa. The form stays open. The cave stays open."

**Scene:** `SCENE_STORY_CAVE` (7) — cave geometry already in `physics.h`.
