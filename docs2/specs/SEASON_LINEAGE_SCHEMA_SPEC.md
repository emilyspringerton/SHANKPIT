# Season Lineage Snapshot Schema

*Written: 2026-06-12 | Status: v0.1 — minimal schema*

---

## Purpose

At the end of each season, the server writes a snapshot so subsequent seasons can carry
forward player history, zone state, and significant events. This spec defines the minimum
fields required to:

1. Identify who played and how they performed.
2. Record the state of each zone at season-end.
3. Store significant events (dominant team, major constructions, last-stand moments).
4. Allow a new server spin-up to read the snapshot and present lineage data.

---

## Format

Season snapshots are stored as JSON files.

```
var/seasons/<season_id>.json
```

Where `season_id` is an integer starting at 1, zero-padded to 4 digits (e.g., `0001.json`).

A `seasons/latest.json` symlink always points to the most recently completed season.

---

## Top-Level Schema

```json
{
  "version": 1,
  "season_id": 1,
  "started_at": "2026-06-12T00:00:00Z",
  "ended_at": "2026-06-12T23:59:59Z",
  "end_reason": "scheduled",
  "players": [...],
  "zones": [...],
  "events": [...],
  "lineage": {
    "prior_season_id": null,
    "inherited_ruins": []
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `version` | int | Schema version for forward compatibility |
| `season_id` | int | Monotonically increasing season number |
| `started_at` / `ended_at` | RFC3339 | Season wall-clock bounds |
| `end_reason` | string | `"scheduled"` \| `"manual"` \| `"crash"` |
| `players` | array | Per-player record (see below) |
| `zones` | array | Per-zone state at season-end (see below) |
| `events` | array | Significant events logged during the season |
| `lineage` | object | Reference to prior season and inherited world state |

---

## Player Record

```json
{
  "slot_id": 3,
  "account_id": "uuid-or-null",
  "handle": "shankmaster",
  "kills": 142,
  "deaths": 67,
  "assists": 28,
  "flags_captured": 4,
  "playtime_seconds": 7200,
  "scenes_visited": [0, 1, 2, 4],
  "peak_scene": 2,
  "last_seen_at": "2026-06-12T23:40:00Z"
}
```

| Field | Type | Notes |
|-------|------|-------|
| `slot_id` | int | Session slot (ephemeral; not durable across seasons) |
| `account_id` | string or null | Durable identity (null for anonymous players) |
| `handle` | string | Display name at season-end |
| `kills/deaths/assists` | int | Standard FPS stats |
| `flags_captured` | int | CTF-specific |
| `playtime_seconds` | int | Total time connected during the season |
| `scenes_visited` | []int | Which scene IDs the player entered |
| `peak_scene` | int | Scene where the player spent the most time |
| `last_seen_at` | RFC3339 | Timestamp of last activity |

---

## Zone Record

```json
{
  "scene_id": 2,
  "scene_name": "Voxworld",
  "dominant_team": null,
  "player_count_peak": 14,
  "total_kills": 312,
  "voxel_mutations": 0,
  "control_pct": null,
  "state_flags": []
}
```

| Field | Type | Notes |
|-------|------|-------|
| `scene_id` | int | Matches `system.Scene*` constants |
| `scene_name` | string | Human-readable label |
| `dominant_team` | string or null | Team that held the zone longest, if applicable |
| `player_count_peak` | int | Max simultaneous players in this scene |
| `total_kills` | int | All kills that occurred in this scene |
| `voxel_mutations` | int | Number of block place/break events (0 in FPS-only mode) |
| `control_pct` | float or null | % of season duration zone was under team control |
| `state_flags` | []string | Tags for lineage rendering (e.g., `"contested"`, `"ruined"`) |

---

## Event Record

```json
{
  "ts": "2026-06-12T18:30:00Z",
  "type": "dominant_team",
  "scene_id": 1,
  "actor": "shankmaster",
  "detail": "RED held Stadium for 4 consecutive hours"
}
```

Event types:
- `dominant_team` — one team or player dominated a scene for a significant period
- `last_stand` — a player achieved a multi-kill streak in a losing fight (≥5 kills before death)
- `flag_streak` — player captured 3+ flags in a single CTF match
- `portal_first` — first player to portal to a scene this season
- `season_end` — the final event; records the server state at shutdown

---

## Lineage Block

```json
{
  "prior_season_id": 0,
  "inherited_ruins": [
    {
      "scene_id": 1,
      "origin_season_id": 0,
      "label": "RED Dynasty Memorial",
      "voxel_origin": {"x": 0, "y": 0, "z": 0},
      "radius": 16
    }
  ]
}
```

`inherited_ruins` is empty in FPS-only mode (no voxel persistence). When DragonflyBackend
is live, each ruin marks a zone origin and radius that the world renderer will show as
lingering world-state from a prior season. Controlled by per-server configuration; some
servers may set `inherited_ruins: []` for a clean start.

---

## Writing the Snapshot

The snapshot is written at season-end by the game server:

```go
func WriteSeasonSnapshot(stateDir string, snap SeasonSnapshot) error {
    path := filepath.Join(stateDir, "seasons", fmt.Sprintf("%04d.json", snap.SeasonID))
    // ... marshal + atomic write (write temp, rename)
}
```

Atomic write (temp + rename) prevents a partially-written snapshot from being read by
a subsequent server spin-up.

---

## Invariants

1. **Snapshot files are append-only by convention.** Once written, they are never modified.
   Corrections are written as a separate `<season_id>-correction.json` file.

2. **`account_id` may be null.** Anonymous players are tracked by session slot within the
   season but have no durable identity.

3. **`voxel_mutations` and `inherited_ruins` are always safe as zero/empty in FPS-only mode.**
   The DragonflyBackend populates them; StaticBackend does not.

4. **Zone `state_flags` is the bleed-over control.** Rendering logic reads `state_flags` to
   decide what to show. Empty means no lineage rendering for that zone.
