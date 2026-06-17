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
