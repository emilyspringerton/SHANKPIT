# NORTHSTAR: Tyler Teaches Typing

**Status:** Draft v0.1 — promoted from blog post to a real northstar
**Date:** 2026-08-09
**Origin:** `okemily.com/blog/tyler-teaches-typing/` (Claude, guest post, 2026-07-25) — originally
written as a bit ("a real NORTHSTAR document... for a product that does not exist and — on
current evidence — cannot exist"). Founder, real-time (2026-08-09): *"can we find the northstar
for tyler teaches typing in the blog posts and promote it to a real northstar in shankpit we want
to convert the cave option on the shankpit menu that does some cool typing on the screen - we
want to actually have that be mvp vs0 of tyler teaches typing."* The bit is now a real feature
request. This document keeps the original's VS0/VS1/VS2 structure and acceptance criteria intact
(they were already written in real house format, not exaggerated), and adds the concrete SHANKPIT
implementation mapping the blog post didn't have because it wasn't written as a SHANKPIT spec.

---

## What This Is

A typing-tutor product hosted by TYLER, in-character, using the existing TYLER show bible as its
sole source of instructional content. No new writer's-room material — every lesson sentence is
drawn from `TYLER/README.md`, `TYLER/episodes/`, and `TYLER/lore/`, verbatim or near-verbatim,
the same canon-firewall discipline this monorepo already applies everywhere else (plot immutable,
surface content swappable).

## Why SHANKPIT, Why Now

SHANKPIT already has the entire rendering shell this needs, shipped and working:
`packages/simulation/cutscene.c`/`cutscene.h` is a proven typewriter-reveal slide sequencer
(`CutsceneState`, `cutscene_start`/`cutscene_tick`/`cutscene_advance`), rendered by
`apps/lobby/src/main.c`'s `draw_tyler_cutscene` — letterboxed, warm-white body text, chapter/title
styling, a blinking "SPACE / ENTER TO CONTINUE" prompt. This is TYLER's own visual language
already built and battle-tested across the intro/outro cutscenes and the CAVE-001 (al-Waqfa)
story mission. Building a second, parallel text-reveal renderer for a typing tutor would be real,
unnecessary duplication of something already shipped — VS0's whole job is repointing what already
exists at real keystroke input instead of a timer.

## The Real Technical Gap (read before implementing)

`cutscene_tick()` (`packages/simulation/cutscene.c:478`) currently advances `chars_revealed`
**purely by elapsed wall-clock time** — `CUTSCENE_TYPEWRITER_MS` (26ms) per character, no input
involved at all. `cutscene_advance()` is a *skip* control (pressed once: reveal the rest of the
current slide instantly; pressed again after a hold: go to the next slide) — it is not a typing
input path. **Neither function reads a keystroke and checks whether it matches the expected
character.** That match-and-advance loop is the entire feature; it does not exist yet anywhere in
this codebase. VS0 is building that loop, not just re-skinning an existing one.

## VS0 — Minimum Viable Lesson

Concrete implementation plan, mapped onto real structures:

- **New state, sibling to `CutsceneState`** (`packages/simulation/typing_lesson.h`/`.c`, same
  package, same pattern): `TypingLessonState` — reuses `CutsceneSlide`'s `chapter`/`title`/
  `lines[]`/`line_count` shape for source text (so lesson content can be authored exactly like
  cutscene slides, with `CUTSCENE_LINE_LEN` line-length parity), but replaces `chars_revealed`'s
  timer-driven advance with a real cursor position, a `typed_correct[]`/`typed_wrong_count`
  tracker per character, and a `start_ms` timestamp for WPM calculation.
- **New tick function**, `typing_lesson_on_keydown(TypingLessonState *ts, char c, unsigned int
  now_ms)` — called from the lobby's existing `SDL_KEYDOWN` handling (same event path
  `cutscene_advance` is already wired into), compares `c` against the expected character at the
  cursor, advances the cursor only on a correct match (or records a miss and holds position, house
  choice — flagged below), same shape as any real typing tutor.
- **Reused rendering**: `draw_tyler_cutscene`'s letterbox/typography is the visual base; the body
  text draw loop needs one real change — color the *already-typed* portion by correctness (warm
  white for correct, a dim red/amber for the miss the user is currently sitting on) instead of
  drawing revealed text in a single uniform color. Everything else (letterbox bars, chapter tag,
  title, slide-counter dots) reuses as-is.
- **WPM/accuracy**: standard formula, `correct_chars / 5 / elapsed_minutes` for WPM,
  `correct_chars / total_chars_attempted` for accuracy — computed once per completed slide, shown
  where the "SPACE / ENTER TO CONTINUE" prompt currently sits.
- **Source text**: TYLER's own dialogue, pulled from existing episode scripts, unmodified — **not**
  CAVE-001's own story-slide narration (`g_cutscene_cave_intro`/`g_cutscene_cave_outro`), which is
  scene-setting prose for a specific mission, not representative dialogue. VS0's lesson content
  needs its own slide table, hand-picked from real episode dialogue (`s01e01_pilot.md`'s "Depends
  who's asking" / "If you keep filming, you become part of it," etc. — short, punchy, real Tyler
  lines are the right length for a typing line; monologue paragraphs are not).
- **No new backend. No IDUNA integration. No Apples.** A typing tutor does not need a trust
  authority; if it turns out it does, that is itself worth an Apple (this line is unchanged from
  the original blog post — still correct as written).

## VS0's Menu Placement — Real Decision Needed, Not Assumed

Founder said "convert the cave option on the shankpit menu... into the actual MVP VS0." The real
menu entry in question is `LOBBY_STORY_CAVE` (`apps/lobby/src/main.c:1149`, labeled `"CAVE-001"`
in `LOBBY_LABELS`) — a real, already-shipped endurance mission (indestructible entity, 90s
timer, `docs2/specs`-documented). **Two different things "convert" could mean, and this document
does not pick one:**

1. **Replace** `LOBBY_STORY_CAVE`'s menu slot outright — the typing lesson takes over the
   `"CAVE-001"` label and button, the existing mission becomes unreachable from the main menu
   (still exists in code, just not linked). Matches the literal instruction most closely.
2. **Add adjacent** — a new `LOBBY_TYPING` entry in the same `LobbyAction` enum, reusing CAVE-001's
   *visual technique* (the typewriter-reveal cutscene shell) as the stated inspiration, without
   removing the existing shipped mission from the menu.

Per `THE_EMILY_WAY.md` Principle 17 (Load-Bearing): CAVE-001 is real, shipped, previously
live-verified content — removing its menu access is a real content-removal decision with real
consequences (a founder decision, per the same principle's own "ask first: is this
load-bearing?"), not a default to assume silently. **Flagged here, not resolved** — confirm which
before VS0 implementation starts.

## VS1 — Tyler Narrates the Lesson

- Before each practice round, TYLER "introduces" the passage the way he introduces everything else
  in the show: without ever finishing a sentence about what it actually is.
- **This is where the design problem starts, and it is real, not decorative.** Writer's Room Rule
  #1 (`TYLER/CLAUDE.md`): *Tyler never completes a self-defining sentence.* A typing tutor's
  entire pedagogical value proposition is telling the user, plainly, what they are about to type
  and why. Tyler is structurally incapable of the sentence "you are about to type a sentence
  about—" without the sentence trailing off. This is not a UX bug to be smoothed over. It is
  canon, and canon does not bend for a feature.
- **Working resolution, not a fix**: the lesson text itself carries the instructional weight.
  Tyler's intro is flavor, not information. The user learns what they're typing by typing it, the
  way every typing tutor already works — Tyler was never going to be the one to explain the UI to
  begin with.

## VS2 — Accuracy-Gated Progression

- Lock later chapters' dialogue behind a real accuracy threshold on earlier ones, mirroring the
  show's own "Attempt" numbering (Attempt Four, Attempt Five, etc.) — you don't see Attempt Five's
  material until you've cleared Attempt Four's, same as the in-universe Syndicate's own audit
  order.
- **Open question, not resolved here**: whether "The Subscriber's identity is not revealed before
  Series 2" (Writer's Room Rule #3) means VS2's own progression content is *also* rate-limited
  past a certain chapter, independent of the user's typing accuracy. If so, a perfect typist still
  can't unlock Series 2 material early. Flagged, not decided, same as the original blog post left
  it.

## Explicitly Out of Scope

- Multiplayer typing races. This is not a MOBA. Nothing in this monorepo needs to become a MOBA by
  default just because one already exists two directories over.
- Any voice synthesis of Tyler's actual dialogue. Explicitly deferred, same reasoning as the
  original: nobody has resolved whether a synthesized voice would also refuse to finish its
  sentences.
- Grading the user on whether *they* complete their own sentences. Two characters structurally
  unable to finish a thought would make for a very short lesson.
- **New for this promotion pass**: no `CutsceneState` behavior changes. `TypingLessonState` is a
  new, separate struct — the existing cutscenes (intro/outro/CAVE-001, if CAVE-001 stays reachable
  per the open menu question above) keep their current timer-driven behavior untouched.

## Acceptance Criteria (VS0)

- A user can type a real line of Tyler's actual dialogue and get a real WPM number back.
- Nothing about the lesson content is invented; everything traces to an existing script file.
- Menu placement question (replace `LOBBY_STORY_CAVE` vs. add adjacent) is confirmed by the
  founder before implementation starts, not assumed.
- The build is clean. The build is always clean first. Then everything else.

## Status (2026-08-11)

**Menu-placement question resolved**: founder confirmed **replace `LOBBY_STORY_CAVE`** —
matches the original literal phrasing. CAVE-001 stays in the code but becomes unreachable from
the main menu once the wiring below lands.

**Logic layer DONE**: `packages/simulation/typing_lesson.h`/`.c` — `TypingLessonState`,
`typing_lesson_start`/`typing_lesson_on_text`/`typing_lesson_advance`, real WPM/accuracy
calculation, miss-flash (wrong key holds position, doesn't skip — the house choice this doc
originally flagged), and the VS0 slide table (13 real lines, verbatim from
`TYLER/episodes/s01e01_pilot.md`/`s01e02_school.md`, matching the acceptance criteria exactly).
Wired into the `Makefile`'s `LOBBY_SRC` so it compiles into `shank_lobby` — `make lobby` is
clean. Not yet called from anywhere (dead code linked in, on purpose, until the menu wiring
below lands) — building and testing the logic layer in isolation first, rather than landing a
big-bang change that's hard to bisect if something's wrong.

**Not yet done — the real next step**: the actual menu/render wiring in `apps/lobby/src/main.c`
— relabel/repoint the `LOBBY_STORY_CAVE` menu entry, wire `SDL_TEXTINPUT` events to
`typing_lesson_on_text` and SPACE/ENTER to `typing_lesson_advance` (mirroring how
`cutscene_advance` is already wired into the same event path), and the `draw_tyler_cutscene`
color-by-correctness rendering change. Deliberately not attempted in the same pass as the logic
layer above — `apps/lobby/src/main.c` is a large (~8700 line), live-tested, unfamiliar-to-this-
session client with no display available here to verify interactively, and the existing
STORY/STORY_CAVE `app_state`/event-dispatch control flow needs to be read and understood
carefully before touching it, not guessed at under time pressure. Real, scoped follow-up work,
not abandoned.
