# Crafting report: GFD + SURVIVAL (WIP, for founder review)

2026-11-28. Status: **paused for scope review.** The GFD side is shipped and live. The SURVIVAL crafting build was planned and launched,
then stopped before it wrote any code: PARENA has no crafting commits and no uncommitted changes. Pick it up from section 3 once the
scope is settled.

## 1. What is live now (GoblinFoxDragon, auto-deployed to play.okemily.com, deploy runs green)

| Commit | What |
|---|---|
| `6f5266a` | Mob XP x0.618 x0.618 (about 0.382), Notorious Monsters included, applied before the party split and mod bonuses |
| `65fcd21` | Starter crafting tier + recipes for every legacy rare drop (scripts/gen_lore_items.py `starter_and_rare`) |
| `eec0908`, `475c100` | VS13 Source Material Lineage: `server/lineage` + MUD wiring + `provenance` command |
| `03819cc` | DEVLOG entry (6) with the VS13 done-definition status |

- **Why progression felt like "only a sword":** about 70 mob drops fed no recipe. That included every first-zone drop (worm sinew, rabbit hide,
  bone chip, bat wing, mouse whisker, owl pellet, vole pelt) and every legacy NM drop (Wyrmund, Lady Sanguine, the Sunderworm brood,
  and the Owl Token that about 45 Notorious Monsters drop).
- **Starter tier (levels 2-7):**
  - Sinew Thread and Cured Rabbit Hide.
  - Rabbit-Hide Cap, Mitts and Jerkin.
  - Bone-Chip Knife and Bone-Chip Hatchet; Sinew Shortbow.
  - Whisker Earring, Pellet Charm, Bat-Wing Cape, Vole-Fur Boots.
  - Every crystal these need drops in zones 0-3. After this tier, the existing copper -> brass -> iron book takes over.
- **Rare-drop gear:**

  | Source | Gear |
  |---|---|
  | Wyrmund | Wyrmund's Carapace, King-Sinew Bow |
  | Lady Sanguine | Sanguine Fang, Sanguine Choker |
  | Sunderworm brood | Sunderworm Plate, Brood-Shell Helm, Sunderworm Core Ring |
  | Owl Token | Owl-Token Signet (3 tokens), Parliament Mantle (5 tokens) |

  HQ crystals crack into 5 normal crystals. The legacy drop ids were kept on purpose: IDUNA inventory saves are upserts, so renaming
  them could duplicate stock players already hold.
- **VS13 ledger:**
  - Lots are born from DROP, HARVEST, CRAFT, VENDOR, REWARD, TRANSFER or ISSUE.
  - Crafting consumes real lots, and the result's parents are exactly those lots.
  - Each auction-house listing holds its own lots, so a buyer traces back to the real seller and then to the mob.
  - UNTRACKED appears only as a parent. Pre-ledger stock is shown honestly rather than given an invented origin.
  - The log is `var/material-ledger.jsonl` and is replayed at boot.
  - Not recorded yet: consumable use, plain trades, mog-house and equip events. There is also no GM grant command (the ISSUE class is
    unused) and no ledger compaction.
- **Not done on GFD:** nothing has been tried in the live game yet. Only Go tests cover it, including one end-to-end test from a rabbit
  kill to a cured hide to the trace.

## 2. The SURVIVAL request, as heard

> "ensure we have basic crafting interface in survival ... take the wood we have and build the same tools as in minecraft with the same
> grid ... the craft command from the GFD side ... a wow style quick craft menu"

The plan below is what I launched. **The founder says the scope is a bit wrong.** Mark it up before anything is built.

### 2a. Planned scope (not built)

1. **Items:** Log (the existing wood count, G 622), Planks, Stick, Crafting Table, and the Wooden Sword, Pickaxe, Axe, Shovel and Hoe.
   Each tool has 59 uses.
2. **Recipes:** exact Minecraft Java wooden-tier shapes and yields.
   - 1 Log -> 4 Planks
   - Plank over Plank -> 4 Sticks
   - 2x2 Planks -> Crafting Table
   - Pickaxe `PPP/.S./.S.`, Axe `PP/PS/.S` (and its mirror), Shovel `P/S/S`, Sword `P/P/S`, Hoe `PP/.S/.S` (and its mirror)
   - Shaped matching: trim to the bounding box, mirror, any position, and an extra item means no match.
   - The rules would go in a new pure file `PARENA/stdlib/shankpit/craft_rules.prn`, which the C client could share.
3. **The grid:** 2x2 always. 3x3 only within about 6 units of a placed crafting table. Tables are drawn-only props with no map box,
   up to 4, reset by a new run or a zone hop. Below the grid is a bag strip; tap a stack to hold it, tap a cell to place one, and tap the
   result slot to craft. A "x All" option and closing the window return grid items to the bag. Item icons would be pixel art.
4. **Quick craft (WoW tradeskill window):** a recipe list with icon, name and "[N] craftable", greyed when N is 0 and tagged "needs table".
   The selected row shows reagents as have/need, plus Create, Create All and -/+ quantity. It would be one window with GRID and QUICK tabs.
5. **Keys:** I opens the grid, K opens quick craft, Esc closes, Tab switches tabs. A new touch button, code 11 "Craft", next to Build.
6. **While the window is open:** the run keeps going and zombies keep coming. The hero does not move, fire or look; clicks go to the
   window; `sp_wants_lock` returns 0, so the host releases the mouse.
7. **Tool effects:** used automatically from the bag. Axe x2 chop damage, Pickaxe x2 chest damage, Sword +melee damage on zombies (likely
   needs a new `mod-melee-damage` hook). Shovel and Hoe would be craftable but do nothing, since there is no dirt or farmland.
8. **"craft" from the GFD side:** a chat line `craft <name> [n]` is offered to the wasm first through a text mailbox (`sv_craft_mbox()`,
   `sv_craft_chat(count)` returning -1 / 0 / 1 / 2 / 3, with the reply line at +256). A -1 means "not a SURVIVAL recipe", and the
   line goes to GFD unchanged, so `craft copper-ingot` still reaches the MUD.
9. **Persistence:** the bag survives a zone hop like wood does and resets on a new run.

### 2b. Questions to settle before building (my reading of where the scope may be off)

- **One craft system or two?** Is SURVIVAL crafting a separate local Minecraft economy, or should it feed the GFD economy? For example,
  SURVIVAL logs could become GFD materials with VS13 lineage, a HARVEST lot from a SURVIVAL tree. Today SURVIVAL wood never reaches the
  server. If SURVIVAL and GFD are one world, crafting probably belongs on the server.
- **"The craft command from the GFD side":** does this mean (a) chat routing as planned, (b) GFD's own recipe book shown or usable inside
  SURVIVAL, or (c) SURVIVAL recipes added to GFD's book so `craft wooden-pickaxe` works in the MUD too?
- **Tools vs weapons:** SURVIVAL weapons are the shankpit gun slots. Should a Wooden Sword become a weapon slot, or stay an auto-used
  bonus? Should tools need to be held, as in Minecraft?
- **Crafting table:** a placed world object (planned, drawn-only) or just an item in the bag? Should it persist per zone?
- **Wood tier only, or also stone and iron?** Those need new resources (stone from rubble?) that SURVIVAL does not have.
- **Pause:** should the game pause while the window is open (single player) or keep running (planned)?

## 3. Facts gathered, so the next session does not re-scout

- **Build:**
  - `PARENA/scripts/build_shankpit_survival_wasm.sh <out.wasm>`, module list in `stdlib/shankpit_survival/MODULES`.
  - `wasm-ld --export-all` makes every defn an export (`-` becomes `_`).
  - No imports and no allocator; fixed 8 MB.
  - `scripts/build_netkit_web.sh <outdir>` builds `nk-host.js` and `survival.wasm` for the phone.
- **Free space:**
  - G words 640-649 and 680-699 looked free (survival uses 600-669, the camera 650-669). Verify before using.
  - The SV region is `0x80000`-`0x100000`: colours +0, guns +1024, chests +2048, slots +4096, trees +6144, barricades +7680,
    GFD/crystal mailbox +12288 (536576, read by the host). +16384 (540672) was the proposed craft region.
- **Hooks:**
  - The contract is `stdlib/shankpit460/sp_mods.prn` (no-ops for the arena). A new hook goes in both that file and the survival side.
  - The survival side is `sv_mod.prn`, with `sv_camera.prn` for `mod-click!`, `mod-cmd!`, `mod-camera!` and `mod-wheel!`.
- **Wood:**
  - `sv_trees.prn` handles chopping and barricades. The numbers are in `stdlib/shankpit/tree_rules.prn`: knife chop 40, katana 70, and
    a barricade costs 5.
  - `sv_zone.prn` keeps wood across a zone hop; `sv-start!` resets it.
- **Keys taken in play:**
  - WASD, arrows, Space, C/Ctrl/Shift, R, E (ability), B (barricade), G (teleport), Esc, M (mute, host).
  - I (73) and K (75) appeared unused. Check again.
- **Phone host (`stdlib/netkit/host/nk_host_survival.prn`):**
  - Chat sends every line unchanged to GFD (`on-sv-chat-send`).
  - Canvas taps outside phase 1 become menu clicks (`sp_pointer` + `sp_button`).
  - Mouse capture is handled by `surv-want-capture?`.
  - The touch-button codes table is in `stdlib/netkit/nk_garage.prn` (`nk-survival-touch-key`, codes 1-10).
- **Desktop host:** `stdlib/shankpit460/sp_host.prn`; `sync-lock!` exits pointer lock when the phase leaves 1.
- **Phone bundle:** `SHANKPIT/apps/netkit/` (index.html, nk-host.js, survival.wasm, PROVENANCE).
  - The buttons in index.html are hand-edited (Build is `data-sv="7"`).
  - The biome work (PARENA `6a6ed82`, `3b4904a`) is **not in this bundle yet**: it still needs a `build_netkit_web.sh` run and copy.
- **Tests:**
  - `node tests/shankpit_survival_test.mjs <wasm>`, `..._zone_test.mjs`, `..._camera_test.mjs`.
  - The "frame under 16 ms" check flakes under load; rerun it once.
- **Tools:** no `emily` CLI on this box, so there were no observe, backlog or Apple steps this session.

## 4. Picking it back up

1. Founder marks up section 2.
2. Settle the questions in 2b.
3. Rerun the build. The stopped workflow script is reusable: two builders (wasm and host) in parallel, three adversarial reviewers, one
   fixer. Edit its spec text to the new scope.
4. Rebuild the phone bundle into `SHANKPIT/apps/netkit` with the biomes and crafting, add the Craft button, and update PROVENANCE.

## 5. Security note (unrelated, still open)

- `~/var-20261128T021420Z-1-001.zip` holds an SSH private key and the Vultr and Cloudflare API keys in plain text.
- `~/github` holds a plaintext GitHub token.
- Delete both and rotate the keys.
