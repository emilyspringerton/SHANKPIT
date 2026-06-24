# TRAPX — Product Northstar

*Codename: TrapX*
*Product name: TRAPX*
*Created: 2026-06-24*
*Studio: Rock Boss Studios × The Danowski Group × EINHORN_INDUSTRIAL*

---

## What This Document Is

The authoritative northstar for TRAPX: a 3D voxel urban sandbox — GTA-like third-person and
first-person, full RPG class system, TRAPX city simulation backend — built on the
GoblinFoxDragon (DragonsNShit) voxel engine with SHANKPIT as the FPS client layer.

This document is the cross-system synthesis. Per-system design lives in:
- TRAPX wiki: `github.com/emilyspringerton/TRAPX/wiki` (city sim, factions, art direction)
- GFD northstar: `GoblinFoxDragon/docs2/MMO_NORTHSTAR.md` (RPG engine systems)
- SHANKPIT northstar: `SHANKPIT/docs2/NORTHSTAR.md` (FPS client, netcode, portal travel)

Emily Prime reads this document. All TRAPX work derives from it.

---

## The Three-Sentence Version

TRAPX is a 3D voxel urban sandbox — GTA-like third-person and first-person — where a living
Detroit-coded city (powered by the GoblinFoxDragon engine) watches, remembers, and reacts to
everything the player does through layered simulation systems (Watchers, Cops, Media, Federal
Oversight), while the player advances through a full 22-class RPG system, claims Field Offices,
deploys K9 swarms, and escalates through a tech tree that makes them the city's most dangerous
variable. The classes are urban-flavored but GFD-mechanically grounded — some locked behind
deep quest chains that require sustained world engagement to unlock. The city is the protagonist;
the RPG is how the player grows inside it.

---

## What TRAPX Is

### The Product

A 3D voxel urban action RPG sandbox. Third-person default, first-person toggle.
The world is a living city generated from TRAPX simulation rules — destructible, persistent,
evolving across sessions. Players start as observers, cross into intervention by claiming
Field Offices, and deepen through an RPG class system that changes how the city responds
to them.

**Three interlocking pillars:**

1. **City Simulation** — TRAPX systems (Watchers/Cops/Media/Neighborhoods) run continuously,
   shaping the world regardless of player action. The city is never idle.

2. **FIELDOFFICE Intervention** — players claim custody nodes, generate Flow, defend against
   Pressure, escalate through the K9 tech tree, and eventually trigger existential crises
   that force alliances.

3. **RPG Progression** — 22 jobs, full stat/skill/gear/XP/crafting systems from GFD,
   urban-flavored but mechanically identical to the DragonsNShit MMO engine. Some jobs
   require deep quest chains to unlock.

### The Feel

**Third-person** (default): GTA3/San Andreas-era orbit camera. Player character in frame.
City readable. Drive, walk, run, take cover.

**First-person** (toggle): SHANKPIT native. Snappy. For tight spaces, FO raids, K9 command.

**Combat**: hybrid FPS + RPG. SHANKPIT physics govern projectile travel and hit detection.
GFD combat systems (TP, weapon skills, skill chains, status effects, enmity) govern
resource management, damage calculation, and ability execution.

**The city feels like it doesn't care you're there.** That is the design.

---

## The Broadcast Meta-Frame

*"TRAPX isn't a game you boot. It's a broadcast you tune into."*

The CRT television frame is the game's entire identity layer.

### Title Screen
- Old CRT frame visible
- Static noise overlay (shader, no assets required)
- Channel 11 indicator
- *"Take Control"* — flicker button; drops you in

### In-Game HUD
- Scanlines (persistent, subtle)
- Broadcast overlay style — never modern clean-UI
- Heat/Attention: signal interference on screen, not meters
- Receipts: ticker-tape overlay, not pop-ups
- Enforcement level: static increase on screen edge

### Transitions
- Static → world fade on session entry
- Distortion on major state changes

### Long-Term
- Multiplayer TVs in-world: watch other players' cities
- Hacked channels: faction propaganda on in-world screens
- Competing city-state transmissions on rival frequencies

---

## The Engine Stack

```
┌─────────────────────────────────────────────────────┐
│                  TRAPX Client                        │
│  Third-person orbit camera + first-person toggle     │
│  CRT broadcast frame (shader/CSS)                    │
│  Receipt ticker overlay                              │
│  SHANKPIT FPS physics (projectiles, hit detection)   │
└────────────────────────┬────────────────────────────┘
                         │ UDP (SHANKPIT netcode)
                         ▼
┌─────────────────────────────────────────────────────┐
│       GoblinFoxDragon / DragonsNShit Server          │
│                                                      │
│  WORLD                                               │
│  ├── Voxel city world (scenes 200–299)               │
│  ├── Zone/scene system + portal travel               │
│  ├── WorldAPI + ProceduralWorldStore (city terrain)  │
│  └── WorldCrisis (threat ladder driver)              │
│                                                      │
│  CITY SIMULATION (TRAPX systems)                     │
│  ├── server/watcher/    — alertness/bias/trust       │
│  ├── server/enforcement/ — 5-level cop state machine │
│  ├── server/media/      — narrative/sentiment/myths  │
│  ├── server/neighborhood/ — personality + mood       │
│  └── server/citymemory/ — cross-session persistence  │
│                                                      │
│  FIELDOFFICE SYSTEMS                                 │
│  ├── server/fieldoffice/ — FO claim/contest/flip     │
│  ├── server/k9/         — dog units + swarm math     │
│  ├── server/attention/  — per-FO Attention meter     │
│  ├── server/integrity/  — Control Integrity + Rogue  │
│  ├── server/techpressure/ — doom clock + 5 threats   │
│  └── server/ledger/     — receipt log + anti-exploit │
│                                                      │
│  RPG ENGINE (GFD MUD systems)                        │
│  ├── server/job/        — 22 jobs + sub-job system   │
│  ├── server/xp/         — level cap + XP             │
│  ├── server/combat/     — TP, weapon skills          │
│  ├── server/skillchain/ — 14 resonances, magic burst │
│  ├── server/status/     — buffs/debuffs (10 kinds)   │
│  ├── server/enmity/     — hate table per mob/FO      │
│  ├── server/quest/      — quest journal + turn-in    │
│  ├── server/gear/       — 16 equipment slots         │
│  ├── server/craft/      — 8 craft types + HQ         │
│  ├── server/market/     — auction house              │
│  ├── server/fame/       — nation/faction reputation  │
│  ├── server/merit/      — merit points (L75+)        │
│  ├── server/party/      — party + alliance + XP chain│
│  ├── server/nm/         — notorious mob spawns       │
│  ├── server/loot/       — treasure pool              │
│  ├── server/mob/        — mob AI + aggro types       │
│  ├── server/homepoint/  — death/raise + home crystal │
│  ├── server/conquest/   — territory control (legacy) │
│  ├── server/field/      — survival guides / XP bonus │
│  ├── server/pet/        — BST pet + K9 handler       │
│  ├── server/guild/      — linkshell / crew system    │
│  ├── server/chat/       — say/tell/yell/crew         │
│  ├── server/food/       — food buff system           │
│  ├── server/gather/     — mining + fishing           │
│  └── server/idunaauth/  — JWKS ES256 JWT validation  │
│                                                      │
│  PERSISTENCE                                         │
│  └── IDUNA (identity, char, items, city memory,      │
│              FO custody records, receipts)            │
└─────────────────────────────────────────────────────┘
```

---

## The City World

### Voxel Urban Environment

The city is a destructible, persistent voxel world generated by GFD's ProceduralWorldStore
and DragonflyChunkGenerator. Urban terrain replaces the MMO fantasy biomes:

| GFD biome | TRAPX urban equivalent |
|---|---|
| Meadow (scene 0) | Residential blocks (low-rise, high cohesion) |
| Hills (scene 1) | Commercial corridor (mixed-use, high traffic) |
| Caves (scene 2) | Underground / industrial underbelly |
| Swampville (scene 3) | Abandoned district (high decay, high NM spawn) |
| TRAPX scenes 200–299 | Full city districts (Detroit-coded zoning) |

### City District Types

| District type | Function |
|---|---|
| Apartment blocks | Citizen spawn, heat accumulation, social density |
| Abandoned houses | Chaos nodes; firebomb targets; rumor/myth seeds |
| Warehouses | Corridor operatives attract; cohesion reducers |
| Highways | Hard voxel walls; neighborhood dividers |
| Party stores | Infrastructure nodes; day/night; food desert anchors |
| Field Offices | Player-capturable; Flow generators; Attention attractors |

### Citizens (NPC Archetypes)

Built on GFD mob system. Behavioral, not representational. Identity via behavior, not labels.

| Archetype | GFD type | Role |
|---|---|---|
| Classic Wanderers | mob.Passive | Roam, react to chaos |
| Distribution Operatives | mob.NeutralAggressive | Corridor travel; volatility reducer |
| Civilians | mob.Passive | Commute, flee; Media perception input |
| Enforcement | mob.Aggressive (threshold) | Cops; activated by Watcher state |
| K9 Unit | mob.Special | Deployed by players; custody organism |

---

## The Simulation Systems

### Layer 0: Simulation (Continuous, No Player Required)

Apartments spawn citizens; accumulate Heat; seed neighborhood personality.
Abandoned houses decay and burn. Warehouses attract operatives. Highways divide.
The city produces emergent storytelling before the player touches anything.

*"If it's not compelling before interaction, it won't be compelling after."*

### Layer 1: The Watchers

Non-visual, non-hostile. Memory modifiers only.

```c
WatcherState {
    watcher_alertness  int  // 0–100
    watcher_bias       int  // -50 (apathetic) to +50 (paranoid)
    watcher_trust      int  // -100 to +100
}
```

Alertness rises: rapid FO expansion, Heat spikes, crew actions, collapses.
Alertness falls: quiet periods, stable holds, lay-low.
Trust tracks HOW the player acts, not what they do.

In 3D: manifests as NPC behavior change, ambient sound shift, crowd density drift.
**No Watcher meter in v1.**

### Layer 2: Enforcement (Cops)

Activates at: Alertness ≥ 65, Trust ≤ -20, Heat ≥ 50.

| Level | Effect in 3D world |
|---|---|
| 0 | No cop presence |
| 1 | Increased Heat accumulation |
| 2 | Cop cars patrol; FO defense harder; monitored blocks |
| 3 | Cop foot presence; block degradation accelerates |
| 4 | Suppression; near-impossible conditions; manage losses |

Player interaction: behavioral adaptation, not direct combat. Route changes, pattern shifts,
visibility reduction. At Level 4: winning = damage control.

### Layer 3: Media

Activates at: Enforcement ≥ 2, Alertness ≥ 75, significant event.

In 3D: in-world TVs and radio stations show player coverage. Newspapers on sidewalks.
Tracked: Narrative Pressure (0–100), Sentiment (-100 to +100), Saturation, Myth count.
Myths mutate neighborhood archetypes across sessions. Media locks interpretations, not bodies.

### Layer 4: Federal Oversight

Slowest. Triggered by Tech Pressure threshold. When active:
module scarcity, Flow taxation, Shadow Operator contracts appear, Coverage market opens.

---

## The RPG System

### Foundation: GFD Engine

The RPG layer is the GFD MUD RPG system — all packages already built and tested.
Urban-flavored names and aesthetics; same mechanical foundation.

**What this means in practice:**
- Players have `server/job` jobs (22 classes), levels, XP, gear, HP/MP pools
- Combat uses `server/combat` TP, `server/skillchain` weapon skills and magic bursts
- `server/status` buffs and debuffs apply in street encounters
- `server/enmity` governs which mob/cop/K9 unit targets which player in group play
- `server/quest` powers the class unlock chain and all urban quest content
- `server/gear` 16-slot equipment system with item level
- `server/craft` 8 craft types (urban equivalents: Fabrication, Electronics, Logistics, etc.)
- `server/market` AH for Flow, modules, Coverage contracts, craftables
- `server/fame` tracks faction reputation (TRAPX factions replace Nations)
- `server/party` + `server/guild` = crew + linkshell system

### The 22 Jobs (Urban-Flavored)

Urban flavor names are aesthetic; GFD mechanics are unchanged.

| GFD Job | TRAPX Urban Identity | Unlock |
|---|---|---|
| WAR (Warrior) | Enforcer | Default |
| MNK (Monk) | Brawler | Default |
| WHM (White Mage) | Street Medic | Default |
| BLM (Black Mage) | Hacker | Default |
| RDM (Red Mage) | Operator / Fixer | Default |
| THF (Thief) | Ghost / Infiltrator | Default |
| PLD (Paladin) | Shield Bearer | Default |
| DRK (Dark Knight) | Hitman | Quest-gated: dark narrative chain |
| BST (Beastmaster) | K9 Handler | Quest-gated: K9 Doctrine initiation |
| BRD (Bard) | Broadcaster / DJ | Quest-gated: Media faction access |
| RNG (Ranger) | Sniper / Scout | Default |
| SAM (Samurai) | Blade Runner | Quest-gated: precision combat trial |
| NIN (Ninja) | Shadow | Default |
| DRG (Dragoon) | Drone Pilot | Quest-gated: surveillance drone craft |
| SMN (Summoner) | Avatar Caller | Quest-gated: all 7 city entity avatars |
| BLU (Blue Mage) | Absorber | Quest-gated: survive 15 unique mob encounters |
| COR (Corsair) | Risk Taker | Quest-gated: AH volume threshold |
| PUP (Puppetmaster) | Automaton Handler | Quest-gated: Fabrication craft rank 80 |
| DNC (Dancer) | Street Performer | Default |
| SCH (Scholar) | Archivist / Intel | Quest-gated: map all 5 city district types |
| GEO (Geomancer) | City Reader | Quest-gated: Neighborhood personality mastery |
| RUN (Rune Fencer) | Ward Runner | Quest-gated: defeat all Oversight Sect bosses |

### Quest-Gated Class Unlock Design

Quest-gated classes require sustained world engagement to unlock. This is not a gate for
exclusivity — it is a gate for story. Each unlock chain reveals a piece of the city's hidden
layer.

**Examples:**

*DRK unlock chain*: Three quests each requiring moral ambiguity — enforce a collection during
a Media narrative saturation spike; complete an Audit without receipts; survive a Rogue Swarm
alone. Reward: DRK job stone + "The city has no heroes" myth added to city memory.

*BST (K9 Handler) unlock chain*: Requires Tier 3 tech tree + completing "Doctrine Initiation"
— a questline about the origin of the K9 program within the city's history. Unlocks direct
K9 command in Escort Mode (sub-job: K9 Handler grants K9 Escort abilities to any main job).

*SMN (Avatar Caller) unlock chain*: Each of the 7 city entity avatars requires a separate
encounter quest in its district — defeating or negotiating with a district's manifestation.
Grants Blood Pact abilities that summon city-scale effects (not fantasy creatures; urban
phenomena: a fire event, a media blackout, a power surge, a fog of confusion).

**The 7 City Entity Avatars (canonical):**

| Avatar | District | Manifestation | Blood Pact Effect |
|---|---|---|---|
| **Baphomet** | Abandoned (scene 204) | Decay lord; horned shadow entity; haunts burned-out voxels; final darkness before a district dies | Rogue Swarm: autonomous pack frenzy in target zone; 3-minute duration |
| **The Anchor** | Industrial (scene 202) | Warehouse colossus; freight hooks for arms; city's economic gravity | Flow Pulse: all allied FOs emit doubled Flow for 60s |
| **Frequency Ghost** | Residential (scene 200) | Static blur; speaks in broadcast fragments; older than the signal | Media Blackout: 90s dead zone — Media layer pauses; no myth seeding |
| **The Warden** | Commercial (scene 201) | Authority specter; badge-shaped face; commands the enforcement layer | Enforcement Reset: drops target district Enforcement by 2 levels immediately |
| **Smoke Tongue** | Underground (scene 203) | Condensation entity; lives in ceiling vents and drain pipes; no fixed form | Fog of Confusion: 120s Watcher blind spot; alertness readings return 0 for duration |
| **Static King** | City-wide (triggered during Tech Pressure ≥ T3) | Broadcasts on every frequency simultaneously; cannot be seen, only heard | Crown Signal: all districts' Pressure decays 30% instantly |
| **The Dragon** | Everywhere / None | The city's own intelligence; cannot be summoned by S1 SMN; requires L75 + all other 6 avatars defeated | Dragon's Eye: Emily Prime (the Dragon GM) acknowledges the summoner — city-scale event chosen by Dragon |

**Baphomet is the first avatar quest.** It lives in the Abandoned district (scene 204) and is
always the entry point of the SMN unlock chain. Baphomet's encounter quest: survive 10 minutes
in the Abandoned district with Control Integrity below 20 and no allied FO held. Defeating
Baphomet (or lasting the full 10 minutes) grants Blood Pact: Rogue Swarm and unlocks the
next avatar encounter. The Dragon is only available post-game (L75, all 6 prior avatars complete).

*GEO (City Reader) unlock chain*: Player must reach Neighborhood personality influence
thresholds in all 5 district types. GEO abilities can then read and shift neighborhood
personality axes mid-session.

*RUN (Ward Runner) unlock chain*: All four Oversight Sect bosses must be defeated or
diplomatically resolved. Ward Runner abilities provide rune-based resistance to Media
narrative hardening and Enforcement escalation.

### Sub-Job System

Same as GFD: sub-job grants half-level stat contribution.
`setjob <JOB> / setsubjob <JOB>`.
K9 Handler as sub-job unlocks Escort abilities for any main.
Street Medic (WHM) as sub-job allows any class to cast Cure spells.

### Merit Points (L75+)

1000 Merit XP per merit point, cap 30. Spend on urban specializations:
Flow efficiency, Attention decay rate, K9 battery life, Enforcement resistance.

---

## The FIELDOFFICE Systems

*(Full spec in prior section; condensed here for reference.)*

**Field Office loop**: Claim → Flow → Pressure → Watcher alert → Enforcement → defend
with tech tree → K9 Attention → Control Integrity decay → Rogue Swarm → Great Alliance →
scar.

**Canonical vocabulary**: Field Office, Flow, Pressure, Claim, Contest Window, Custody Lock,
Attention, Receipt. Never "trap house."

**K9 Doctrine (Tier 4)**: diminishing returns (`0.85^n`), 4-phase Merciless Operation,
3 counterplay lanes, swarm flip only in Contest Window.

**Tech Pressure doom clock**: 5-tier existential threat ladder (Leash Frays → Crown Protocol).

**Rogue Swarms**: forced cross-faction alliance event; 3 containment objectives; district scar.

---

## Faction Reputation (server/fame adapted)

TRAPX factions replace GFD's three nations. Fame package mechanics unchanged.

| TRAPX Faction | Role | Rank gate |
|---|---|---|
| The Frequency | Creative power; knowledge; inner-city influence | Rank 2: advanced quests |
| The Bloc | Working-class street stability; high-tolerance districts | Rank 1: basic |
| Procurement Houses | Shadow Operator class; Coverage markets | Rank 3: expensive access |
| Oversight Sects | Federal enforcement layer; antagonist (no player alignment) | N/A |
| Media Apparatus | Narrative faction; news coverage; myths | N/A |

Players choose one alignment at start; faction fame gates advanced quests and abilities.
Switching factions is possible but expensive (fame loss + quest chain required).

---

## Art Direction

*Full spec: TRAPX wiki Art Direction Cohesion Specification*

**Prime Directive**: *"The art must never look like it wants your approval."*

**3D target**: GTA3-era fidelity. Not GTA V. Not Cyberpunk. Urban, readable, slightly
uncomfortable. Hard geometry. No ambient occlusion. No depth of field. No camera shake.
Flat directional lighting. Hard shadows only.

**Color**: information, not decoration. Asphalt = muted blues/greys. Buildings = off-white/
concrete/dull brown. Abandoned = darker, desaturated. Fire = harsh orange, rare, permanent.
Citizens = slightly brighter than environment.

**Voxel city**: chunk geometry must read as district type within 5 seconds. Apartment blocks:
repetitive vertical stacking. Warehouses: large, boxy, highway-adjacent. Abandoned: dark,
degraded voxel state.

**Test**: *"Does this look like it belongs in a system that doesn't care if the player
is watching?"*

---

## Milestones

### M0 — Engine Proof
SHANKPIT third-person orbit camera. TRAPX city scene (ID 200) with basic voxel urban
geometry. First-person toggle. Player character in frame.
CRT broadcast frame + "Take Control" title screen.
*Acceptance: can walk a city block in third-person; toggle to FPS.*

### M1 — City Lives (GFD simulation core in urban world)
Apartment buildings, citizen archetypes, abandoned houses, warehouses, highways in voxel city.
Citizen AI (wander, react, flee). Abandoned houses burn emergently (no player trigger).
Neighborhood personality axes live. City runs idle.
*Acceptance: 5-minute idle city shows diverging neighborhood behavior.*

### M2 — City Remembers (Watcher memory + IDUNA persistence)
WatcherState per district. alertness/bias/trust persist via IDUNA city memory.
Neighborhood mood drift. End-of-session: *"The city noticed how you moved."*
*Acceptance: second session on same city reflects first session's choices.*

### M3 — City Pushes Back (Cops + Media in 3D)
Enforcement threshold triggers; cop NPCs spawn and patrol; 5 enforcement levels with
distinct world changes. In-world TVs and radios carry Media narrative. Myths emerge.
*Acceptance: player climbs Enforcement 0→3 and visibly feels world stiffen.*

### M4 — Take Control (FIELDOFFICE + RPG base)
Player claims a Field Office. Flow/Pressure loop. Contest Windows. Basic K9 Sentry.
Receipt ledger. All 22 base GFD RPG systems wired (job, XP, combat, status, gear).
First quest chains (3 base classes, 2 quest-gated).
*Acceptance: claiming an FO changes the city; player levels from 1 to 30.*

### M5 — Quest-Gated Classes (deep unlock chains)
All 8 quest-gated class unlock chains implemented (DRK, BST, BRD, SAM, SMN, BLU,
GEO, RUN). Merit system live at L75. Sub-job system live.
Party + crew system live. Faction reputation (fame) tracking active.
*Acceptance: DRK unlock chain completable; SMN avatar quests for all 7 entities.*

### M6 — K9 Full Doctrine + Rogue Swarms
Full swarm mechanics. 4-phase Merciless Operation. Counterplay lanes.
Control Integrity + Rogue Swarm event. Containment objectives. Scar system.
Tech Pressure doom clock live. All 5 threat tiers triggerable.
*Acceptance: a Rogue Swarm forces cross-faction alliance to contain it.*

### M7 — Party Stores + Full Economy
Party store buildings (day/night; supply variability; merchant unfaction).
Full AH economy: Flow, Coverage contracts, Blackbox modules, crafted gear.
8 craft types with urban flavors. Mining = materials salvage. Fishing = scavenging.
*Acceptance: party store closure measurably changes district movement patterns.*

### M8 — Ship (Steam / itch.io)
Multiplayer (2–16 players). Persistent city across sessions. CRT frame polished.
In-world screens show rival crew receipts and Media coverage.
Steam release candidate.

### M9 — Season 2 (post-launch)
New district expansion. Live event (real-world trigger → in-world city event).
Community build contest (player-modified voxel city structures persist).

---

## Open Questions (hold for input)

**Design:**
1. Player character: pre-built protagonist or character creator?
2. Vehicles: drivable city (GTA-style) or foot/transit travel only?
3. ~~SMN urban avatars~~ — RESOLVED: 7 avatars canonical. Baphomet (Abandoned), The Anchor (Industrial),
   Frequency Ghost (Residential), The Warden (Commercial), Smoke Tongue (Underground), Static King
   (city-wide/T3), The Dragon (L75 post-game only). See §SMN unlock chain above.
4. BRD Broadcaster: do songs affect Media narrative pressure?
5. Distribution Operatives: playable class or NPC-only? (maps to NIN/THF?)
6. Party store content: does player operate them as merchants (COR class mechanic)?

**Business:**
7. Rock Boss Studios / The Danowski Group: ownership, contribution, rev split?
8. TRAPX music artist: diegetic radio stations, ambient soundtrack, or collaboration credit?
9. Standalone Steam vs. SHANKPIT expansion?
10. Multiplayer model: shared persistent city or rival city-state competition?
