# Party Stores — TRAPX Vertical Slice 2 Design Document

**Status**: Planned — intentionally out of scope for Vertical Slice 1.  
**Scope**: Design direction and rationale. No implementation obligation until VS2.

---

## What a Party Store Is

In Detroit and across many urban neighborhoods, "party store" is the local term for a convenience store — often a small independent shop that serves as the primary provisioning point in food deserts. In TRAPX, party stores are not modeled as shops. They are **infrastructure**.

A party store is:

- The primary provisioning node in blocks with no grocery access
- A social gravity well: people converge here across different time windows
- An informal information exchange: who's around, what's moving, who's watching
- An early indicator of neighborhood stress or recovery — they are among the first to close and the last to reopen

They sit at the intersection of citizens, merchants, substances, time-of-day, and enforcement attention.

---

## Why They Matter to the Simulation

Party stores answer one question:

> **What happens when a neighborhood's only infrastructure is barely enough?**

They are slow systems. They create pressure, not events. They make consequences legible after the fact. A block that loses its party store doesn't explode — it quietly hollows out. That hollowing is the signal.

---

## Planned Mechanics

None of the following are implemented in Vertical Slice 1. They are documented here to establish direction.

### Hours of Operation

- Tied to a global day/night cycle (MMO-style clock)
- Stores have open/closed state with consistent schedules per merchant
- Closed stores change neighborhood flow: foot traffic reroutes or collapses
- Forced closures (enforcement pressure, violence nearby) create secondary effects on citizen routing and fear

### Supply Variability

- Limited fresh food stock; high availability of convenience goods (snacks, lottery, single-serve liquor)
- Supply chain state degrades under sustained TechPressure or district stress
- Blocks with degraded supply develop dependency patterns visible in citizen behavior over time

### Foot Traffic Memory

- Repeated visits by the same citizen increase local familiarity with the merchant
- Familiarity creates information pathways: a familiar citizen might tip the merchant to patrol activity
- Traffic patterns form slowly — they become observable before consequences attach to them

### Substance Vectors

- Certain goods create indirect behavioral effects on citizen NPCs (energy levels, aggression, compliance)
- Delayed impact on media and oversight layers — not immediate signals
- Substance availability is a lever, not a combat mechanic

---

## Merchants as an Unfaction

Party stores are operated by **merchants** — a persistent actor class that does not map to the traditional faction system.

Merchants:

- Do not seek territory control
- Do not escalate conflict
- Respond to incentives, not ideology
- Have no formal crew affiliation

They persist across neighborhood shifts and outlast factions. A store that opens under one dominant crew will still be there under the next one, unless conditions make it untenable.

### What Merchants Remember

Merchants maintain a lightweight persistent state:

- **Who keeps them solvent** — regular customers who spend reliably; they give these people early closing warnings
- **Who brings trouble** — actors whose presence correlates with enforcement attention or violence; they close early when these actors appear
- **When to close early** — a merchant's read on the night; not always rational, but always consequential

This makes merchants a **stabilizing but fragile force**. They do not fight back. They adapt or close.

### Merchants as Watchers

Merchants can participate in the Watcher system, but they are **low-pressure watchers by default**.

- A merchant's ambient observation is passive — they notice things, but they do not report without cause
- Their Watcher contribution is minimal under normal conditions (low heat, no active threat)
- **Threat threshold**: if a merchant perceives direct threat to their person, store, or livelihood — sustained dog presence, explicit violence nearby, a crew that has marked their store as a vector — their reporting pressure escalates sharply
- Threatened merchants are not reliable informants. They are frightened ones. The signal they produce may be high-volume but low-accuracy: they report what scared them, not necessarily what matters

This differs from the dedicated Watcher archetype. A merchant who reports is burning a relationship, not performing a function. They do it once, or they do it from fear. The system should treat merchant-sourced Watcher signals as high-urgency but low-confidence, and decay them faster than signals from dedicated observers.

---

## Integration with Existing Systems

| System | Party Store Role |
|---|---|
| Neighborhood Mood | Supply degradation raises Fear; closure raises Fatigue |
| TechPressure | Sustained pressure reduces hours; forced closure at Tier 4+ |
| Watcher / Attention | Low-pressure passive observer by default; escalates to active reporting only when threatened — high-urgency, low-confidence signal; decays fast |
| Scar System | Store closure from MercilessOp or Rogue Swarm is a Scar-eligible event |
| K9 / Enforcement | Dog presence near store accelerates merchant's early-close threshold |
| Citizen AI | Provisions need drives citizens toward open stores; routes around closed ones |

---

## Scope Guardrail

To be explicit about what is **not** in Vertical Slice 1:

- No party store entities, merchants, or inventory systems exist in the engine
- No inventory UI is planned
- No direct player interaction with stores is required in VS1
- No substance effects are modeled in any detail

This document is a **north star, not a promise**. It establishes design intent so that VS1 architecture decisions do not accidentally foreclose VS2 mechanics.

---

## Open Design Questions (for VS2 planning)

1. Does the merchant have a persona (name, voice, history) or is it a pure simulation node?
2. How does the player interact — trade, information, or only ambient observation?
3. What triggers a permanent store closure vs. a temporary forced close?
4. How are multiple stores in the same district differentiated (specialty, reputation, hours)?
5. Does merchant state persist across server sessions (part of the persistent world engine)?

---

*Authored 2026-06-25 — TRAPX VS2 planning pass.*
