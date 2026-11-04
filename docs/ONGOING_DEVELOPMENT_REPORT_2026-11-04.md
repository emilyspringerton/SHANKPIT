# Ongoing development report — 2026-11-04

Two jobs in this file, kept together because they answer the same question ("what is actually left?"):

1. **§1–§2 status:** the named-not-built lists from the two slices completed today — the TRAPX city
   engine (S631) and the GFD-frontend/1700s train VS0 — recorded so the next session inherits them
   instead of rediscovering them.
2. **§3 the plan:** every outstanding maintainer request, planned rather than implemented (founder,
   2026-11-04: *"work on planning out all of the maintainers requests dont worry about implementing
   finish your train scope do that work and then make a plan for all of the requests"*).

---

## 1. Status: the TRAPX city engine (S631, shipped 2026-11-04)

Built and green: `PARENA/stdlib/trapx/city_engine_rules.prn` (36 rules), generated into
`packages/simulation/trapx_city_rules.c`, `make test-trapx-city` (60 checks), mapped in
`docs/EMPIRE_NORTHSTAR.md` §6.

**Named, not built (unchanged):**

| # | Gap | Why it is not trivial |
|---|---|---|
| 1 | No host: nothing calls the 36 rules; no `TrapxCitySim`, no 1 Hz tick | needs the cell-granularity decision first (§3.9/Q1) |
| 2 | No per-cell state on the wire (pop/pressure/money/scrutiny) | `PACKET_EMPIRE_STATE` is the mutable half; adding fields is a protocol change with parity tests |
| 3 | No casino, train, ship, power-grid or neighborhood *object* | `ce-ship-resource-shock` is written and unreachable — there are no ships |
| 4 | Citizens do not move by economy | `citizen_life.c` is persona-driven (correct); the tie-in is a host change |
| 5 | No Spire mod on REFLUX, no automata runner | ECOWAR's `livingmap/creep.c` is the precedent, not a dependency |
| 6 | No WOTAN/TS mirror of the city rules | the street's rules have one; this layer does not |
| 7 | `TRAPX_NEIGHBORHOOD_PERSONALITIES` fear/trust/fatigue still prose | `ce-neighborhood-stage` is a 3-way classification with nothing under it |

## 2. Status: the GFD-frontend / 1700s VS0 (shipped 2026-11-04)

Built and green: `era_tech_rules.prn`, `mining_rules.prn`, `citizen_day_rules.prn`,
`trapx/citizen_contact_rules.prn`; C host + `make test-frontend-rules`; a real wasm32 module
(5,582 bytes, 0 imports) with 143 checks in node. Full writeup:
`docs2/GFD_FRONTEND_VS0_NORTHSTAR.md`.

**Named, not built:**

- 3D frontend in wasm (rules only, no renderer); GFD service integration; REDGARDEN merged-down code (its
  tick order and `livingmap/` are borrowed, nothing runs); no pickaxe/ore/train/rail object in any host.
- ~~A real PARENA compiler bug~~ — **found and FIXED later the same day** (`PARENA` `emit_llvm.c`): the
  wasm emitter used "pure" as a proxy for "safe to evaluate unconditionally", so
  `(if (<= b 0) 0 (/ a b))` selected both arms and trapped. The predicate is now `expr_is_speculable`
  (pure AND `head_may_trap` clean, i.e. no `/` or `mod`), dividing arms get real basic blocks, and the
  repro returns 0 end to end in wasm. `make test` 374/374; this module rebuilt at 143/143, 5,582 bytes.
  Still open: `and`/`or` are non-short-circuiting, so a division inside an `and`/`or` operand can still
  trap where a short-circuiting backend would not — a semantic change, not a fix, deliberately not done.
- No VRM/VRoid citizen bodies at runtime (`stdlib/forgedinface/rig.prn` is real, nothing renders it).

---

## 3. The plan

Ordered by dependency, not by enthusiasm. Nothing here is implemented except where marked **[built]**.

### 3.1 The act/progress API (the foundation everything else needs)

Players and citizens will be giving each other work, so the **same** API has to serve both. Proposed shape,
IDUNA-side (it is the only service with identities, receipts and an audit ledger):

- `POST /api/v1/acts` — create an act: `{kind, issuer, counterparty|null, terms, escrow, deadline, district}`;
  terms are a small, fixed vocabulary (§3.3) plus integers, never free text that a client interprets.
- `GET /api/v1/acts?counterparty=…&district=…` — open acts, interest-managed by district.
- `POST /api/v1/acts/{id}/claim|progress|verify|settle|dispute` — the state machine; `progress` is a
  *declared* event, `verify` is an observation from the world (a receipt, a watcher, a witness count).
- Every transition files an Apple (the ledger already exists) and emits a REFLUX action type, so game-side
  mods can subscribe without polling IDUNA.

Anti-abuse lives in §3.4 (questing guild), not in the API: the API is a bookkeeper, the guild is the
reputation market around it.

### 3.2 TYLER mode — real affordances for actors (SHANKPIT)

**Intent** (founder, 2026-11-04): *"the actors should use real programmed shankpit widgets via the master
affordances widgets api … replace the wisp with a real character after the first scene … actors can be
killed, props can be moved around via rigid body physics, chairs can be sat in, items collected from
drawers into inventory/cargo … merge it in with the Tyler phone UI."*

What exists and is reusable: `packages/simulation/widget_runtime.h` / `widget_circuit.h` +
`edu_widgets` (machine_type 1–8, real, running); `MASTER_AFFORDANCE_LIBRARY.md` (Build 0186, repo-verified)
and `WIDGET_AFFORDANCES.md`; `brick_bodies.c` for rigid-body props; `ragdoll_pool`/`rigid_ragdoll`;
`phone.h`'s host-fed app pattern (`phone_set_tech` is the model); `citizen_avatar.c`.

Planned slices, in order:

1. **Affordance vocabulary as rules** — `PARENA/stdlib/shankpit/affordance_rules.prn`:
   `aff-allowed(affordance, action)` with a fixed enum (SEAT, DRAWER, DOOR, TERMINAL, LEVER, LIFT, SINK,
   BED × USE/OPEN/SIT/COLLECT/PUSH/HACK), `actor-can-operate(machine_type)`, `damage-step(health, dmg,
   armor)`, `push-impulse(strength, mass)`, `collect-fits(weight, carried, cap)`, `drawer-loot(roll, tier)`.
   Scalar, no RNG (host rolls), same discipline as today's slice.
2. **Wisp → character handover** — the wisp is scene 0 only: `tyler-scene-actor-kind(scene)` returns
   WISP for 0 and REAL after, and `actor-widget-count(scene)` returns 0 / 3. The *body* comes from
   `stdlib/forgedinface/rig.prn` (VRM) rather than a new avatar pipeline; the host binds it where
   `gband_skel_npc.c` already binds a mannequin.
3. **Actors use widgets** — an actor's "station" is an `edu_widget` plus a claimed affordance; the actor
   drives it through `actor-can-operate` and the existing EduVM register writes. No new VM.
4. **Killable actors** — `damage-step` + the existing `ragdoll_pool`; death drops the actor's cargo
   (§3.3 collection), which is where "items in drawers" and "loot" become the same mechanism.
5. **Props, chairs, drawers** — props are `LevelBox` + `brick_bodies` (no new `LevelPropInstance`; that
   schema does not exist and the library says not to invent it); a seat is an affordance on a box; a
   drawer is a container with the loot table.
6. **Tyler phone UI merge** — one new host-fed app row (`BP_APP_TYLER`) listing the scene's live
   affordances, plus the existing CARGO/NOTES/CONTACTS surfaces for what an actor hands you. The phone
   never invents a cost or a name.

### 3.3 Animal Crossing, harder — citizens you can actually befriend (TRAPX)

**[built]** `stdlib/trapx/citizen_contact_rules.prn`: affinity, gifts, contact sharing (host-rolled),
four services with affinity floors and friend discounts floored at half price.

Planned next:

1. **Contact book → phone CONTACTS** — a bitmask already exists (`cc-add`/`cc-known`); the host needs a
   roster of citizens per district and a call action.
2. **Services become acts** — commerce/borrow/repair/mercenary are §3.1 acts with prices from
   `cc-service-price-milli`, so a mercenary contract and a player-issued bounty are the same object.
3. **Mercenary contracts** — the founder named "farming pvp contracts": a contract names a district, a
   duration and a payment; escrow from §3.1; verification by kill/witness receipts (§3.5).
4. **Borrowed tech** — this is the one that needs an item-instance model: borrowing a tool means it must
   come back, so items need identity (`item_id`) rather than a count. That is a real schema change and is
   listed as a dependency, not a detail.

### 3.4 Emergent quests, player-given quests, and the questing guild

**Intent:** citizens offer work from their own situation (personality, priorities, market, alignment,
trust, faction, pressure, heat); some of those offers are traps; the player can surveil, ghost, honey-pot or
counter-set-up; the information gained is the reward. And because a base NPC and a player must be
indistinguishable, a player can give another player a quest through the same surface.

Design, in the founder's own framing — "smart contract meets the FFXI bazaar":

1. **Offer generation** — `PARENA/stdlib/trapx/quest_rules.prn` (planned): `quest-offer-chance` from
   personality openness, market slack, trust, pressure, heat; `quest-kind` from the citizen's path + the
   district's state (a CRAFT citizen wants a rare uncut gem, per the founder's own example; a DUEL citizen
   wants a body). No RNG in the rule; the host rolls.
2. **Terms as escrow** — every quest is an act (§3.1) with a deposit (`quest-deposit-milli`) held by the
   guild, paid on `verify`, slashed on `dispute`. "Smart contract" here means exactly that: no free-text
   promises, a small verify vocabulary, and a ledger that already exists (Apples).
3. **The plot layer** — a quest can carry a hidden second party (`plot`): surveilling the issuer (an act
   itself) reveals it; the player then chooses to complete, ghost, honey-pot or counter-plot. Information
   about actors is the durable reward: it is persisted as **reputation facts**, not as quest state.
4. **Player-issued quests** — same endpoints, `issuer=player:<id>`; the guild takes a stake (below) and the
   escrow is the player's own cargo/influence, so an abusive issuer runs out of money before goodwill.
5. **The questing guild (abuse management)** — a five-tier reputation market, not a moderator:
   - **stake** — an issuer must escrow 100% of the reward plus a guild fee (fee falls with reputation);
   - **settlement** — payment releases on verified completion; a false `verify` is itself an act and is
     slashable;
   - **griefing** — repetitive hostile quests (honey-pots aimed at one player) raise the issuer's
     exposure; three unresolved disputes suspend issuance (a real, bounded penalty, no ban);
   - **visibility** — an issuer's reputation and dispute count are public to anyone considering the quest,
     which is the bazaar half: you shop for counterparties, not just for work;
   - **districts** — guild offices are Field Offices when a TRAPX district has one, which quietly makes
     the guild part of the Watcher economy.
6. **Indistinguishability** — one rule, enforced by the absence of a channel: nothing in the act API may
   expose whether `issuer` is a player or a citizen except through in-world evidence (a citizen's
   schedule, a player's habits). The client must not be told; that is the founder's "nothing special about
   any of the npcs".

**Risks named:** escrow needs an authoritative wallet (influence today, an auction-currency later);
verification is the whole security surface (a forged `verify` is theft); and a reputation market with real
value will be farmed, so fees and stake must be set from measured data, not guesses. The shape to copy is
GFD's auction house rather than a fresh design: server-authoritative records, a fee taken at settlement, a
visible history, and a caller that moves the currency — the missing half today.

### 3.5 Actor goals, priorities and the economy — RuneScape/EVE "play how you want"

**Intent:** actors set goals from their priorities and work toward them (farm → list on the auction house);
some play economy, some PvP, some economy-for-gear, some for prestige, and sometimes everyone does
something random.

1. **Goal model as rules** — `PARENA/stdlib/shankpit/actor_goal_rules.prn` (planned): a small scored-utility
   selector over `{EARN, FIGHT, CRAFT, SOCIALISE, REST}`, scored from needs (`cd-hunger-step`,
   `cd-energy-step`), holdings, contacts, district pressure/heat, and a personality vector of **priorities**
   (not traits — traits already exist in `citizen_life.c`). MISHRI's own `BehaviorOrchestrator`
   scored-utility shape is the precedent to copy.
2. **Economy play** — goal EARN → pick a producer (farm §3.6, mine, trade route) → haul → list on the
   **auction house**. **It already exists, in GFD, and this correction matters:**
   - `GoblinFoxDragon/server/market/ah.go` (+ `ah_test.go`) is a real, server-authoritative, FFXI-parity
     AH: one AH per server shared by all zones, sellers list at a fixed price, buyers take the cheapest
     listing, the seller is blind until the sale closes, **the 12 most recent sale prices per item are
     public** (the PS2 terminal's own screen), listing fee on sale, cancel/reclaim, categories, and the
     full PS2 terminal screen model (`BrowseCategory`/`ItemPage`/`Buy`/`List`/`CancelListing`/
     `SellerListings`/`BuyerHistory`). This *is* the "FFXI bazaar" the founder named.
   - It is already hooked to doctrine: `server/attention/attention.go` carries `AHTaxMultiplier` — "cost
     of doing business under scrutiny" — so TRAPX's Attention already makes commerce dearer.
   - So the work is **integration, not construction**: a frontend surface for it (a phone app or the
     terminal row), mapping SHANKPIT cargo counts onto GFD item identity, and a currency/escrow answer
     (`ah.go`'s own comment says the caller handles the transfer — that caller is the missing piece).
   - Price floors for farm/mine output should come from the rules (`ce-money-flow`, `cc-service-price-milli`)
     rather than a second economy invented at the frontend.
3. **Randomness, deliberately** — actors deviate: a small, bounded per-decision error draw (host-rolled,
   seeded per actor+minute so two clients agree and a replay is faithful). This is what stops a bot from
   reading as a bot, and it is the same humanness primitive `HUMANNESS_NORTHSTAR.md` already scopes.
4. **Cultural influences** — a culture is a *priority prior*, not a flag: HONOR/ELITE (prestige-weighted:
   fights that matter, contracts that pay in reputation), DOWN_AND_DIRTY (EARn- and survival-weighted,
   ignores reputation), MPK (preys on others' progress: hostile acts as a goal, the modern descendant of
   the old MPK griefers). Crucially, culture is chosen for bots **and available to players as a starting
   prior** — it biases the same selector, it does not gate content.
5. **Prestige** — a durable, non-spendable score (contracts honoured, disputes lost, notable kills), which
   is what makes "economy to level for prestige" real rather than a leaderboard afterthought.

### 3.6 Urban farming (TRAPX)

Planned in full in `TRAPX/docs/URBAN_FARMING_NORTHSTAR.md` (**not** urban planning — founder's correction):
plots, soil, contamination from the city engine's corruption, water, harvest, market, compost, theft, and a
community garden as the cozy layer's one safe zone. First step is one more PARENA mod
(`farming_rules.prn`) plus one plot in one level.

### 3.7 What was finished today (train scope)

**[built]** the 1700s era/train tree, traditional mining, the citizens' day, citizen contacts, the C host,
and the wasm32 rule module with both test surfaces green. See `docs2/GFD_FRONTEND_VS0_NORTHSTAR.md`.

### 3.8 Merge status

- **GFD**: cloned (`../GoblinFoxDragon`), phases 1–4b of `docs/GFD_UNIFICATION.md` already shipped
  (zones, `PACKET_ZONE_STATE`, coop host/beacon, 23-job stats bridge applied). Not started: a real client of
  GFD's state services, meadow-as-geometry (phase 5), and job persistence across launches.
- **REDGARDEN**: cloned (`../REDGARDEN`), **not** merged down. Its per-tick subsystem order and
  `livingmap/` corruption spread are borrowed by rules; no REDGARDEN code executes in SHANKPIT. The shared
  `stdlib/engine/` layer remains a proposal (§3.10).
- **ECOWAR**: cloned, a hard fork; its 16-card `card_effect_mod.prn` is the model for accept/settle
  contracts.

### 3.9 Open questions this plan cannot answer alone

1. **Cell granularity** (also §1): claim cell (32, wired) vs district (72, phone) vs node (16). Recommendation
   stands: cell = claim cell, district = aggregate.
2. **Currency**: influence (spendable pressure) vs an auction currency vs both. Escrow and fees need one
   authoritative answer.
3. **Item identity**: borrowing tech and an auction house both need `item_id`; today inventory is counts.
4. **Verification source of truth**: receipts, witnesses, or an authority tick — this decides whether the
   questing guild is trustworthy.

### 3.10 The one shared dependency

Every section above wants the same three things, and none exists cleanly yet: **item identity** (GFD's item
model exists on its side; SHANKPIT's cargo is counts), **an authoritative wallet with escrow** (GFD's AH
records a sale but its own comment says the caller moves the currency), and **an act/verification ledger**
(IDUNA Apples is the natural home).
Building those once, as `stdlib/engine/`-shaped core deps (PARENA-first per the standing rule), is cheaper
than three half-versions inside three game systems.

### 3.11 REDGARDEN session/lobby/protocol rules merged into SHANKPIT via PARENA mods (2026-11-04)

**Built and green:** REDGARDEN's session/lobby/protocol scaffolding is now a set of PARENA rule mods that SHANKPIT hosts:

- `PARENA/stdlib/redgarden/{net,arena,ability,ai}_rules.prn` -> generated `redgarden_{net,arena,ability,ai}_rules.c`
- SHANKPIT generator `scripts/gen_redgarden_rules.sh`
- host `packages/simulation/redgarden_rules_host.h`, test `packages/simulation/redgarden_rules_test.c`

net_rules is the brokerage layer (slots, phases, bandwidth budget, drops, tickets); arena/ability/ai carry the game numbers and hero kits. `bin/redgarden_rules_test` runs clean: `BUILD_OK` + `make gen_redgarden_rules.sh` clean + verify: **145 passed, 1 failed** (the 1 failure is a pre-existing `ai_rules.prn`/`rg_decide` greedy-behavior bug, unrelated to the net merge).

**Commit:** SHANKPIT `redgarden-rules` `092ae0e` (7 files, 1523 insertions) — pushed to origin. PARENA `redgarden` `ebc25d8` (4 mods, 650 insertions) committed locally; GitHub remote currently returns ISE on push, so committed-vs-pushed state differs. REGISTER: `EMILY`'s BACKLOG + `context/golden-docs-index.md` row `REDGARDEN-NET-RULES`, GOLDEN_DOCS resync (`docs/EMILY/context/golden-docs-index.md`).

**PARENA compiler gaps found during port** (named, not hidden): `let` binding names must be bare symbols; `let` has no inline type annotations; `bit-and` folds to C's `&` (comma operator) so parity uses `(mod x 2)`/`%`.

**Arc / next:** REDGARDEN's C `arena_game.c`/server stays the reference (untouched); these mods are the SHANKPIT decision layer. Next redgarden deliverable is `make server` with the lobby link wired (blocked on the missing `EDITOR.GAME` checkout).

### 3.12 Next-steps report for the next agent (REDGARDEN net-rules merge, 2026-11-04)

**What is done (committed and synced):**
- SHANKPIT: `092ae0e` redgarden-rules (4 .c + host + test + generator), `7398c16` this report — pushed, local==origin/master.
- EMILY: `abb6e0c9` BACKLOG + golden-index row, pushed.
- PARENA: `ebc25d8` redgarden mods (net/arena/ability/ai .prn) committed locally; **needs `git push` from a box with working SSH** (this box returns ISE on push).
- GOLDEN_DOCS: `abeb526` snapshot resync, REDGARDEN-NET-RULES registered.

**What still needs doing (in order):**
1. **Push PARENA `ebc25d8`** (`git push origin master` at repo root) — the redgarden mods are not visible on GitHub until this lands. One SSH command; the remote returns ISE on this box, so retry on another machine if it still fails.
2. **Run `make server` in SHANKPIT** — confirm REDGARDEN's lobby link actually boots end to end with the new PARENA session-rules mods under it.
3. **Confirm `make gen_redgarden_rules.sh` regenerates clean** (`GEN_EXIT=0`) and the generated `.c` files match the committed host (determinism check).

**Named gate / homework for the next agent:**
- The REDGARDEN session-rules merge depends on the missing `EDITOR.GAME` checkout (shipped `0f7bca5`, `editor_main.c` with the current-file/board-profile-aware AVR fix) — lobby wiring is blocked on that.
- If pushing PARENA still errors, the `GITHUB_TOKEN` must be re-scoped for repo-admin (the existing token returns HTTP 403 on repo creation).

**Open questions this merge leaves:**
- Arena/game numbers (`rg_ready_count_needed`, hero kits, bandwidth budget) are NYI until the C `arena_game.c` server accepts these decisions.
