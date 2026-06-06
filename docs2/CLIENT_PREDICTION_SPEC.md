# Client Prediction, Reconciliation, and Remote Interpolation

*Last updated: 2026-06-06*

This document describes the exact implementation in `apps/lobby/src/main.c` for:
1. Client-side prediction of the local player
2. Server reconciliation when an authoritative position arrives
3. Two-snapshot interpolation of remote players

---

## Critical Constraint: INTERP_DELAY_MS vs Snapshot Interval

The two-snapshot interpolator computes:

```
t = (render_ts - a_time) / (b_time - a_time)
```

For `t ∈ [0, 1]`, `render_ts` must fall *between* the two snapshot timestamps `a_time` and `b_time`. This requires:

```
INTERP_DELAY_MS < snapshot_interval_ms
```

Current values:
- `SERVER_SNAPSHOT_INTERVAL_TICKS = 2` → `2 × 16 ms = 32 ms`
- `INTERP_DELAY_MS = 24 ms` (0.75 × 32 ms)

At `INTERP_DELAY_MS = 24 ms`, a snapshot arriving at T produces a render timestamp of `T - 24 ms`. With a 32 ms snapshot interval, this lands at `t ≈ 0.25` just after arrival, and reaches `t ≈ 1.0` when the next snapshot is 8 ms away. The interpolator stays in [0.25, 1.0] for the entire 32 ms window — smooth, no extrapolation.

**Wrong values that were previously in the code:**
- `INTERP_DELAY_MS = 220 ms` with 48 ms interval → `t` always < 0 (before snapshot a), pure extrapolation
- `INTERP_DELAY_MS = 100 ms` with 48 ms interval → same problem, t always < 0

The rule: keep `INTERP_DELAY_MS` at ~0.5–0.75× the snapshot interval. Never exceed the interval.

---

## Remote Player Interpolation

**Code:** `net_apply_remote_interpolation()` in `apps/lobby/src/main.c`

Each remote player slot `rinterp[i]` stores:
- `a` — older snapshot position/yaw/pitch + `a_time_ms` (server timestamp)
- `b` — newer snapshot position/yaw/pitch + `b_time_ms` (server timestamp)

On each tick:
1. Compute `synced_now = now_ms + net_server_time_offset_ms` (local clock → server time)
2. Compute `render_ts = synced_now - INTERP_DELAY_MS`
3. For each remote player with both `a` and `b`: `t = (render_ts - a_time) / (b_time - a_time)`, clamped to [0, 1]
4. Lerp position; angle-lerp yaw (handles 359→1 wrapping); lerp pitch

If only `a` (no `b`): use `a` directly. If `b_time <= a_time`: use `b` directly.

**Time synchronization:** `net_server_time_offset_ms` is computed from the `WELCOME` packet server timestamp: `offset = server_time - local_time`. This converts local millisecond timestamps into server-relative ones for snapshot matching.

---

## Client-Side Prediction

**Code:** `client_apply_cmd_movement()` → calls `shankpit_simulate_movement_tick()`

On each tick in `STATE_GAME_NET`:
1. Sample keyboard/mouse into `UserCmd`
2. Apply movement to `local_state.players[my_client_id]` immediately (prediction)
3. Store `cmd` in `client_cmd_hist[seq % CLIENT_RECON_HISTORY]`
4. Send `cmd` via UDP at `CLIENT_USERCMD_INTERVAL_MS` rate

Commands are sent with `NET_CMD_HISTORY = 5` redundancy (last 5 cmds in each packet) to survive packet loss without re-requesting.

The local player position seen by the renderer includes `reconcile_corr_{x,y,z}` offsets that decay exponentially each tick (lambda = `RECONCILE_DECAY_LAMBDA = 12.0`):

```c
float keep = expf(-RECONCILE_DECAY_LAMBDA * SHANKPIT_NET_FIXED_DT);
reconcile_corr_x *= keep;
```

---

## Server Reconciliation

**Code:** `client_reconcile_local_player()` in `apps/lobby/src/main.c`

Triggered on each snapshot that contains authoritative data for `my_client_id`.

Algorithm:
1. Save current predicted position as `prev_{x,y,z,yaw,pitch,vx,vy,vz}`
2. Set player position to server's authoritative `{auth_x, auth_y, auth_z, auth_yaw, auth_pitch}`
3. Re-simulate all commands from `ack_seq+1` to `net_latest_seq_sent` via `client_apply_cmd_movement()`
4. Compute error: `e = prev - replayed_prediction`
5. If `|e| > RECONCILE_HARD_SNAP_DIST (2.0 m)` or `|yaw_err| > RECONCILE_HARD_SNAP_YAW (45°)`: discard and hard-snap (zero correction)
6. Otherwise: add error to `reconcile_corr_{x,y,z,yaw,pitch}`, capped at `RECONCILE_CORR_MAX (1.2 m)`:

```c
float corr_mag = sqrtf(reconcile_corr_x² + reconcile_corr_y² + reconcile_corr_z²);
if (corr_mag > RECONCILE_CORR_MAX) {
    float scale = RECONCILE_CORR_MAX / corr_mag;
    reconcile_corr_{x,y,z} *= scale;
}
```

The cap prevents runaway correction accumulation when the player teleports or significant RTT spikes cause many stale commands to replay.

**Y-axis dead zone:** If the player is on-ground and `|ey| < 0.15 m`, the vertical error is zeroed before adding to correction. This avoids correction noise from terrain step-down snapping (which moves `y` by up to 0.55 m to clamp onto downslopes).

---

## Constants Summary

| Constant | Value | Purpose |
|---|---|---|
| `SERVER_SNAPSHOT_INTERVAL_TICKS` | 2 | Server sends snapshot every 2 ticks × 16 ms = 32 ms |
| `INTERP_DELAY_MS` | 24 | Render timestamp offset; must be < 32 ms |
| `NET_CMD_HISTORY` | 5 | Redundant cmds per packet (packet loss robustness) |
| `CLIENT_RECON_HISTORY` | 64 | Circular buffer of cmds for replay |
| `RECONCILE_DECAY_LAMBDA` | 12.0 | Exponential decay of correction each 16 ms tick |
| `RECONCILE_HARD_SNAP_DIST` | 2.0 m | Position error threshold for hard snap |
| `RECONCILE_HARD_SNAP_YAW` | 45° | Yaw error threshold for hard snap |
| `RECONCILE_CORR_MAX` | 1.2 m | Maximum accumulated correction vector magnitude |
| `NET_SERVER_TIMEOUT_MS` | 5000 | Client declares disconnect after 5 s of no snapshots |
| `NET_DISCONNECT_OVERLAY_MS` | 2500 | Overlay shown before auto-return to lobby |

---

## Debugging

Enable at compile time:
- `NET_JITTER_DIAG 1` — prints interp and reconcile stats at 1 Hz
- `NET_PARITY_DEBUG 1` — HUD overlay with per-frame correction/replay telemetry

Log flags (default 0, can be set to 1 for verbose mode):
- `NET_LOG_SNAPSHOT` — every snapshot received
- `NET_LOG_USERCMD` — every command applied on server
- `NET_VERBOSE_LOG` — all CLIENT_LOG/SERVER_LOG calls

`NET_LOG_TIMEOUT` and `NET_LOG_HANDSHAKE` remain on by default.
