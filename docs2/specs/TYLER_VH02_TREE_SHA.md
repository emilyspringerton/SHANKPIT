# TYLER — VALHANNA VH02: "Tree SHA"

**Mode:** MODE_TYLER, local lobby play order (VH01 -> E03 -> FB01 -> **VH02** -> CONSTRUCT)
**Source script:** `TYLER/episodes/vh02_valhanna_tree_sha.md` (TYLER commit `878bb67`; not present
in the TYLER working tree at time of writing, see that repo's history)
**Level:** `assets/tyler_levels/tyler_vh02_tree_sha.json` (same room as VH01, renamed `TYLER_VH02_TREE_SHA`)
**Coordinator:** `packages/simulation/tyler_vh02_coldopen.{h,c}` (mirrors E03/FB01)

## Actor mapping (differs from every other scene)

VH02 has no Tyler or Hana. The two scripted slots are the crew: `TYLER_ACTOR_TYLER` = CAM,
`TYLER_ACTOR_HANA` = SOUND GUY. The PA and Producer (O.S.) are subtitle-only. The slots render with
the level's two existing story-ally kits (kit 2/3) because no crew models exist -- a named stopgap.

## Beat table

| Beat | Actor | Marker | Hold | Subtitle |
|---|---|---|---|---|
| 0 | BOTH | (0,1,9) | 3000 | SOUND GUY: Room tone's weird... It's held. |
| 1 | BOTH | (0,1,5) | 3000 | PRODUCER (O.S.): Don't touch the thermoses... let them talk. |
| 2 | CAM | (4,1,-4.5) | 3000 | PA: "Repo construct." tree_sha: 9b84785... |
| 3 | SOUND | (3,1,-3) | 2500 | What's a tree sha? / A fingerprint for a whole folder. |
| 4 | CAM | (4,1,-4.5) | 3000 | PA: "The documentary is never called a --" [1 WORD REMOVED IN POST] |
| 5 | CAM | (4,1,-4.5) | 3000 | "She would find this appropriate." [SEALED LOG // ENTRY 23] |
| 6 | NONE | -- | 3000 | (28.7 Hz under the floor. The door turns from the other side.) |
| 7 | CAM | (4,1,-2.6) | 2500 | ENCOUNTER: THE CREW MEETS  *(fires exit)* |

## Skins

Cast per scene from the SKIN_* library (`g_tyler_scene_skins`, apps/lobby): VH01 Tyler=TYLER/Hana=ALPINE; E03 TYLER/GHOST; FB01 FROG/UNICORN; VH02 Cam=WANDERER, sound guy=VIKING; CONSTRUCT hero=DUCK. Lobby-local; not compiled in this sandbox.

## Known gaps (named)

- **VO is build-time only.** `TYLER/tts/render_scenes.py` (Piper stopgap) generates it in CI; a local build without it is silent. Subtitles are French + `[EN:]` and committed.
- **No in-game playback yet for ANY scene.** The voice tables, clips and driver exist and are tested, but `apps/lobby` has no mixer call site (VH01 included) -- pre-existing gap.
- **Lobby-local only.** Like E03/FB01, not wired into `apps/server` / `--tyler` (no PACKET_TYLER_BEAT).
- **Exit is the shared button mechanism**: the last beat "presses" the exit button, so the unfinished
  `ENCOUNTER` card cuts to the CONSTRUCT hub rather than to an on-screen reveal. The script's
  encounter is deliberately unnamed upstream; nothing here names it either.
- **Headless-verified only**: `make test-tyler-vo` runs the scene to its exit. The lobby binary was
  NOT built or run (no SDL2 headers in this sandbox; `make lobby` also needs `../EDITOR.GAME/gen`).
- **Real tree_sha is frozen** in the subtitle; it goes stale on the next TYLER commit by design.
