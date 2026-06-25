# TRAPX — Neighborhood Personalities (Micro-Cities)

*Source: TRAPX GitHub Wiki — "Neighborhood personalities" page.*  
*Implementation status notes appended at end.*

---

> The city doesn't react as one thing.  
> It reacts as a thousand small places with memory.

Neighborhoods are persistent micro-simulations with attitudes, habits, and tolerances.
They are the bridge between cellular mechanics and narrative memory.

---

## 1. Core Design Goal

Neighborhoods must:

- Feel distinct without being authored
- React differently to the same player actions
- Accumulate local memory
- Feed into Cops and Media differently

They are personalities, not factions.

---

## 2. Neighborhood Is the Atomic Narrative Unit

Forget "districts" or "zones."

A Neighborhood is:

- 6–25 grid cells
- One name (procedural)
- One dominant personality
- One evolving memory profile

```c
typedef struct {
    int id;
    char name[32];

    // Personality axes
    float tolerance;     // how much instability is ignored
    float pride;         // resistance to outside influence
    float cohesion;      // how fast alignment spreads
    float visibility;    // how easily noticed by Watchers

    // Memory
    int incident_count;
    int last_major_event_tick;
    int local_myths;     // number of myths seeded here

    // Current mood
    float fear;          // raises Cop likelihood
    float trust;         // raises player influence
    float fatigue;       // reduces activity

} Neighborhood;
```

---

## 3. Personality Axes (Small but Expressive)

Each neighborhood is initialized with 4 floats (0.0–1.0):

| Axis | Meaning |
|---|---|
| Tolerance | How much chaos is ignored |
| Pride | Resistance to conversion |
| Cohesion | Speed of alignment spread |
| Visibility | How quickly Watchers notice |

### Example Profiles

**Old Residential**
- High tolerance, high cohesion, low visibility, medium pride

**Downtown Core**
- Low tolerance, low cohesion, high visibility, low pride

**Industrial Fringe**
- High tolerance, low cohesion, medium visibility, low pride

**Historic Block**
- Low tolerance, high pride, medium cohesion, high visibility

No tags. No lore. Just numbers.

---

## 4. How Neighborhoods Affect Core Systems

### Grid Updates

- Alignment pressure is scaled by **cohesion**
- Conversion resistance is scaled by **pride**
- Collapse thresholds shift with **tolerance**

### Watchers

- Watcher alert growth is multiplied by **visibility**
- Low-visibility neighborhoods can stay hot longer

### Cops

- Fear > threshold triggers local enforcement
- Pride reduces compliance effectiveness

### Media

- High-visibility neighborhoods seed myths faster
- Historic blocks generate stronger myths

---

## 5. Neighborhood Memory (Local, Not Global)

Each neighborhood tracks its own history.

**Incident types:**

- Crackdown
- Collapse
- Quiet Takeover
- Failed Intervention
- Long Stability

**Memory effects:**

- Repeated crackdowns raise fear permanently
- Long stability increases trust
- Frequent collapse causes fatigue

This means: two neighborhoods in the same city can live in different timelines.

---

## 6. Mood Drift (The Secret Sauce)

Neighborhood moods drift slowly, not instantly.

Rules:

- Fear decays slowly without enforcement
- Trust grows slowly with consistency
- Fatigue decays only with inactivity

This creates:

- Burned-out neighborhoods
- Over-policed zones
- Quiet strongholds
- Fragile flashpoints

---

## 7. UX: How the Player Perceives This

**Never show stats.**

Instead:

- Slight color shifts
- Ambient sound tone changes
- Text flavor in events

Examples:

> "This block feels tense."  
> "The streets here don't care anymore."  
> "People here remember."

That's it.

---

## 8. Interaction With Media (Critical)

When Media generates a myth, it is anchored to a neighborhood.

Myths propagate outward with decay.

This enables:

- Infamous blocks
- Neighborhood reputations
- Cities with internal contradictions

---

## 9. v1 Scope Lock (Important)

For TRAPX v1, neighborhoods:

- Are auto-generated
- Do not change size
- Do not merge/split
- Do not have named NPCs

That's deliberate.

---

## 10. Why This Matters Long-Term

Neighborhoods enable:

- Player migration strategies
- Turf-based play
- Long-term city evolution
- EVE-style reputation without player counts
- Dwarf Fortress-style emergent storytelling

All without new mechanics.

---

## 11. One-Line Summary

> The city remembers globally.  
> Neighborhoods remember personally.

---

## 12. Natural Next Steps (Optional)

If you want to go further later:

- Named neighborhoods
- Player-affiliated blocks
- District elections
- Infrastructure decay
- Player-driven gentrification
- Real-world city imports

Not needed now.

---

## Implementation Status (GoblinFoxDragon `server/neighborhood/`)

*As of 2026-06-25*

### Implemented ✓

| Feature | Location |
|---|---|
| Personality struct (Tolerance, Pride, Cohesion, Visibility) | `neighborhood.go:Personality` |
| Fear, Fatigue mood axes with drift/decay | `AddFear`, `Drift`, `Tick` |
| WatcherVisibilityMultiplier (Visibility × fatigue penalty) | `WatcherVisibilityMultiplier()` |
| Myth seeding (Fear > 90 + Cohesion > 60 threshold) | `checkMythSeed()` |
| Example personality profiles (Old Residential, Downtown Core, etc.) | `DistrictPersonalities` map |
| Mood drift with configurable decay rates | `MoodDecayConfig`, `Drift()` |
| Scar system (+5% visibility per scar, `RemoveLast` for ScarBurn) | `server/scar/scar.go` |

### Not Yet Implemented — Gap vs. Wiki Spec

| Feature | Wiki Spec | Status |
|---|---|---|
| `Trust` mood axis | float, grows with consistency | Missing from `Neighborhood` struct |
| `IncidentCount` | int, cumulative incident tracker | Missing |
| `LastMajorEventTick` | int, tick of last significant event | Missing |
| Named incident types | Crackdown / Collapse / Quiet Takeover / Failed Intervention / Long Stability | Events use free-form `Verb` strings |
| Grid cell membership | 6–25 cells per neighborhood | No spatial binding yet |
| Permanent fear from repeated crackdowns | Memory effect | Mood decay erases; no permanent floor |
| Trust → player influence coupling | High trust increases player influence radius | Not connected to player state |

The `Trust` axis and the five named incident types are the most load-bearing gaps for VS1 completion. Everything else (grid cells, player influence) is VS2 scope.
