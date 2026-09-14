#!/usr/bin/env python3
"""rl_env_packet.py -- SHANKPIT's real, packet-level RL training environment (S459-48).

Founder real-time: "continue adding stuff to make our bot training pipeline real and work." This
is the single biggest named gap from docs/BOT_TRAINING_NORTHSTAR.md's own §9 -- a packet-level
training harness for SHANKPIT the way BRAWLPIT/scripts/rl_env_packet.py already is for BRAWLPIT.
Same real architecture, deliberately ported rather than reinvented: the observation IS the literal
bytes bin/shank_server's own server_broadcast() sends every real tick over UDP
(packages/common/protocol.h's NetHeader/UserCmd/NetPlayer), and the action IS the literal bytes
apps2/emily-bot's own sendUserCmd sends. A trained policy is therefore a genuine drop-in bot
speaking the real wire protocol -- it could run as a totally separate process or machine with zero
code sharing beyond this file.

Real, byte-exact wire layout -- verified via a compiled sizeof/offsetof C probe against this
exact build during S459-44/45/47 (not guessed), self-checked again here at import time the same
way BRAWLPIT's own ctypes.Structure definitions assert their own sizeof:
  NetHeader=12 bytes, UserCmd=36 bytes, NetPlayer=68 bytes.

Real, deliberate architectural difference from BRAWLPIT's own env, not an oversight: SHANKPIT's
QUEUE mode has NO PacketResetMatch (BRAWLPIT's own env was blocked for a while on exactly this
missing packet, see BRAWLPIT/docs/RL_TRAINING_NORTHSTAR.md) because it doesn't need one -- players
respawn automatically and continuously (apps/server/src/main.c's own death/respawn state machine),
there is no single-match "won/lost" terminal state the way a stock match has. This env therefore
defines one EPISODE as one LIFE: terminated=True the tick the player's own state transitions
ALIVE->DEAD, and reset() (after the very first connect) just waits for the server's own automatic
respawn rather than reconnecting or sending any reset packet -- there is nothing to reset.

Auth: PacketConnect's own real wire format includes a 256-byte JWT field
(apps2/emily-bot/main.go's own sendConnect), but a bot can send it all-zero and connect completely
anonymously -- confirmed live, this is exactly what the standing shankpit-bot-pool.service's own 3
real bots already do every day (no SHANKPIT_AUTH_TOKEN set for them).
"""

import ctypes
import socket
import struct
import sys
import time
from dataclasses import dataclass, field
from typing import Optional

import numpy as np

try:
    import gymnasium as gym
    from gymnasium import spaces
except ImportError:  # pragma: no cover -- real, honest degrade, matches BRAWLPIT's own convention
    gym = None
    spaces = None

try:
    import requests
except ImportError:  # pragma: no cover
    requests = None

# --- Wire protocol constants (packages/common/protocol.h) ---
PACKET_CONNECT = 0
PACKET_USERCMD = 1
PACKET_SNAPSHOT = 2
PACKET_WELCOME = 3

MODE_QUEUE = 108  # S459-34

BTN_JUMP = 1
BTN_ATTACK = 2
BTN_CROUCH = 4
BTN_RELOAD = 8

STATE_ALIVE = 0
STATE_DEAD = 1

WPN_KNIFE, WPN_MAGNUM, WPN_AR, WPN_SHOTGUN, WPN_SNIPER, WPN_KATANA, WPN_MISSILE, WPN_FLASHLIGHT = range(8)

# Real WPN_STATS table (protocol.h) -- (dmg, rof, cnt). Used for the same per-weapon lethality
# score apps2/emily-bot/observation.go's own weaponLethality table computes.
WPN_STATS = {
    WPN_KNIFE:   (200, 20, 1),
    WPN_MAGNUM:  (45, 25, 1),
    WPN_AR:      (20, 6, 1),
    WPN_SHOTGUN: (128, 17, 8),
    WPN_SNIPER:  (101, 52, 1),
    WPN_KATANA:  (40, 28, 1),
    WPN_MISSILE: (130, 95, 1),
    WPN_FLASHLIGHT: (0, 1, 0),
}
_RAW_LETHALITY = {w: (dmg * cnt / rof) for w, (dmg, rof, cnt) in WPN_STATS.items()}
_MAX_LETHALITY = max(_RAW_LETHALITY.values())
WEAPON_LETHALITY = {w: v / _MAX_LETHALITY for w, v in _RAW_LETHALITY.items()}

WEAPON_MAX_AMMO = {WPN_KNIFE: 0, WPN_MAGNUM: 8, WPN_AR: 30, WPN_SHOTGUN: 8, WPN_SNIPER: 5,
                   WPN_KATANA: 0, WPN_MISSILE: 3, WPN_FLASHLIGHT: 0}


# --- ctypes wire structures, self-verifying (same real convention BRAWLPIT's own rl_env_packet.py
# established) ---

class NetHeader(ctypes.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("type", ctypes.c_uint8),
        ("client_id", ctypes.c_uint8),
        ("sequence", ctypes.c_uint16),
        ("timestamp", ctypes.c_uint32),
        ("entity_count", ctypes.c_uint8),
        ("scene_id", ctypes.c_uint8),
        ("_pad", ctypes.c_uint16),
    ]
assert ctypes.sizeof(NetHeader) == 12, ctypes.sizeof(NetHeader)


class UserCmd(ctypes.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("sequence", ctypes.c_uint32),
        ("timestamp", ctypes.c_uint32),
        ("msec", ctypes.c_uint16),
        ("_pad", ctypes.c_uint16),
        ("fwd", ctypes.c_float),
        ("str_", ctypes.c_float),
        ("yaw", ctypes.c_float),
        ("pitch", ctypes.c_float),
        ("buttons", ctypes.c_uint32),
        ("weapon_idx", ctypes.c_int32),
    ]
assert ctypes.sizeof(UserCmd) == 36, ctypes.sizeof(UserCmd)


class NetPlayer(ctypes.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
        ("id", ctypes.c_uint8),
        ("scene_id", ctypes.c_uint8),
        ("is_bot", ctypes.c_uint8),
        ("team_id", ctypes.c_int8),
        ("last_seq", ctypes.c_uint32),
        ("x", ctypes.c_float),
        ("y", ctypes.c_float),
        ("z", ctypes.c_float),
        ("yaw", ctypes.c_float),
        ("pitch", ctypes.c_float),
        ("current_weapon", ctypes.c_uint8),
        ("state", ctypes.c_uint8),
        ("health", ctypes.c_uint8),
        ("shield", ctypes.c_uint8),
        ("is_shooting", ctypes.c_uint8),
        ("crouching", ctypes.c_uint8),
        ("_pad0", ctypes.c_uint16),
        ("reward_feedback", ctypes.c_float),
        ("ammo", ctypes.c_uint8),
        ("in_vehicle", ctypes.c_uint8),
        ("carried_flag_team_id", ctypes.c_int8),
        ("hit_feedback", ctypes.c_uint8),
        ("storm_charges", ctypes.c_uint8),
        ("_pad1", ctypes.c_uint8),
        ("kills", ctypes.c_uint16),
        ("deaths", ctypes.c_uint16),
        ("death_elapsed_ms", ctypes.c_uint16),
        ("death_duration_ms", ctypes.c_uint16),
        ("_pad2", ctypes.c_uint16),
        ("death_dir_x", ctypes.c_float),
        ("death_dir_z", ctypes.c_float),
        ("reload_timer", ctypes.c_uint16),
        ("ability_cooldown", ctypes.c_uint16),
    ]
assert ctypes.sizeof(NetPlayer) == 68, ctypes.sizeof(NetPlayer)


# --- Encode/decode helpers, matching apps2/emily-bot/main.go + snapshot.go byte-for-byte ---

def encode_connect(game_mode: int = MODE_QUEUE, jwt: bytes = b"") -> bytes:
    buf = bytearray(13 + 256)
    buf[0] = PACKET_CONNECT
    buf[12] = game_mode & 0xFF
    if jwt:
        buf[13:13 + min(len(jwt), 256)] = jwt[:256]
    return bytes(buf)


def decode_welcome(data: bytes):
    """Returns (client_id, scene_id) or None if data is too short to trust."""
    if len(data) < 10:
        return None
    return data[1], data[9]


def encode_usercmd(cmd: UserCmd) -> bytes:
    # Wire: [0]=type [1:12]=header-zeros [12]=count=1 [13:49]=UserCmd (36 bytes)
    buf = bytearray(13 + 36)
    buf[0] = PACKET_USERCMD
    buf[12] = 1
    buf[13:49] = bytes(cmd)
    return bytes(buf)


def decode_snapshot(data: bytes):
    """Returns a list of NetPlayer copies, or None if data is too short to trust. Mirrors
    apps2/emily-bot/snapshot.go's own decodePacketSnapshot exactly: entity_count lives at
    NetHeader offset 8, entities start at offset 13 (12-byte header + 1 redundant count byte),
    each entity is sizeof(NetPlayer)=68 bytes. A truncated/overclaiming buffer stops safely at
    however many whole entities actually fit, never misparses past the real buffer end."""
    if len(data) < 13 or data[0] != PACKET_SNAPSHOT:
        return None
    count = data[8]
    out = []
    off = 13
    for _ in range(count):
        if off + 68 > len(data):
            break
        out.append(NetPlayer.from_buffer_copy(data, off))
        off += 68
    return out


# --- Real UDP packet client ---

class PacketClient:
    """A real, minimal UDP client speaking SHANKPIT's actual wire protocol. Blocking with a
    timeout -- a training loop wants a real snapshot before it decides the next action."""

    def __init__(self, host: str, port: int, timeout: float = 5.0):
        self.host = host
        self.port = port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(timeout)
        self.client_id: Optional[int] = None
        self.scene_id: Optional[int] = None
        self._seq = 0

    def connect(self, game_mode: int = MODE_QUEUE) -> bool:
        self.sock.sendto(encode_connect(game_mode), (self.host, self.port))
        deadline = time.time() + 5.0
        while time.time() < deadline:
            try:
                data, _ = self.sock.recvfrom(4096)
            except socket.timeout:
                return False
            if len(data) >= 1 and data[0] == PACKET_WELCOME:
                welcome = decode_welcome(data)
                if welcome is None:
                    continue
                self.client_id, self.scene_id = welcome
                return True
        return False

    def send_action(self, fwd: float, strafe: float, yaw: float, pitch: float, buttons: int, weapon_idx: int):
        self._seq += 1
        cmd = UserCmd(
            sequence=self._seq, timestamp=int(time.time() * 1000) & 0xFFFFFFFF, msec=16,
            fwd=fwd, str_=strafe, yaw=yaw, pitch=pitch, buttons=buttons, weapon_idx=weapon_idx,
        )
        self.sock.sendto(encode_usercmd(cmd), (self.host, self.port))

    # MAX_DRAIN -- real, found-live fix: under --fast-forward (server_apply_custom_level's own
    # tick loop runs unthrottled, verified live at ~500K ticks/sec -- see S459-48's own commit
    # message) the server can produce snapshots far faster than one Python process can drain
    # them, so an unbounded "while True: recv" here never actually terminates -- new packets keep
    # arriving in the kernel socket buffer faster than recvfrom can pull them out. Capping the
    # drain means step() occasionally returns a slightly-stale (by at most MAX_DRAIN ticks, a
    # real, bounded, negligible staleness at 500K ticks/sec) snapshot instead of hanging forever.
    MAX_DRAIN = 256

    def recv_snapshot(self):
        """Blocks for one real packet, then drains the socket buffer non-blocking (up to
        MAX_DRAIN packets) to return a recent snapshot (matches BRAWLPIT's own recv_snapshot real
        "drain toward latest" convention -- bounded here, see MAX_DRAIN's own doc comment, since
        SHANKPIT's own --fast-forward runs dramatically faster than BRAWLPIT's ever did)."""
        try:
            data, _ = self.sock.recvfrom(65536)
        except socket.timeout:
            return None
        latest = data if data and data[0] == PACKET_SNAPSHOT else None
        self.sock.setblocking(False)
        try:
            for _ in range(self.MAX_DRAIN):
                data, _ = self.sock.recvfrom(65536)
                if data and data[0] == PACKET_SNAPSHOT:
                    latest = data
        except (BlockingIOError, socket.error):
            pass
        finally:
            self.sock.setblocking(True)
            self.sock.settimeout(5.0)
        if latest is None:
            return None
        return decode_snapshot(latest)

    def close(self):
        self.sock.close()


# --- Level geometry (real raycast support, mirrors apps2/emily-bot/geometry.go) ---

LEVEL_REGISTRY_BASE_URL = "https://okemily.com/api/v1/shankpit-levels"


@dataclass
class Wall:
    x: float; y: float; z: float
    sx: float; sy: float; sz: float


def fetch_queue_level_geometry():
    """Real, one-shot fetch of the currently-flagged QUEUE default level's box list -- mirrors
    apps2/emily-bot/geometry.go's own fetchQueueLevelGeometry exactly (same registry-lookup-by-
    is_default_queue contract). Returns None on ANY real failure (network unreachable, no default
    flagged, bad JSON, requests not installed) -- a training run with no geometry just gets
    zero-filled raycast features (see build_observation), never blocks or crashes on this."""
    if requests is None:
        return None
    try:
        resp = requests.get(LEVEL_REGISTRY_BASE_URL, timeout=5.0)
        resp.raise_for_status()
        summaries = resp.json()
        level_id = next((s["id"] for s in summaries if s.get("is_default_queue")), None)
        if level_id is None:
            return None
        exp = requests.get(f"{LEVEL_REGISTRY_BASE_URL}/{level_id}/export", timeout=5.0)
        exp.raise_for_status()
        walls_json = exp.json().get("walls", [])
        return [Wall(w["x"], w["y"], w["z"], w["sx"], w["sy"], w["sz"]) for w in walls_json]
    except Exception:
        return None


def _slab(origin, d, lo, hi, tmin, tmax):
    eps = 1e-6
    if abs(d) < eps:
        return (tmin, tmax, True) if lo <= origin <= hi else (tmin, tmax, False)
    t1, t2 = (lo - origin) / d, (hi - origin) / d
    if t1 > t2:
        t1, t2 = t2, t1
    tmin = max(tmin, t1)
    tmax = min(tmax, t2)
    return tmin, tmax, tmin <= tmax


def raycast(walls, ox, oy, oz, dx, dy, dz, cap):
    """Real AABB slab-method raycast against a real box list -- byte-for-byte the same algorithm
    as apps2/emily-bot/geometry.go's own levelGeometry.raycast, verified there with a direct unit
    test (TestRaycast_SimpleWall)."""
    if not walls:
        return cap
    best = cap
    for w in walls:
        tmin, tmax = 0.0, best
        tmin, tmax, ok = _slab(ox, dx, w.x - w.sx / 2, w.x + w.sx / 2, tmin, tmax)
        if ok:
            tmin, tmax, ok = _slab(oy, dy, w.y - w.sy / 2, w.y + w.sy / 2, tmin, tmax)
        if ok:
            tmin, tmax, ok = _slab(oz, dz, w.z - w.sz / 2, w.z + w.sz / 2, tmin, tmax)
        if ok and 0 < tmin < best:
            best = tmin
    return best


# --- Observation (mirrors apps2/emily-bot/observation.go's 84-feature vector exactly) ---

NEAREST_OPPONENT_SLOTS = 4
SELF_FEATURES = 26
PER_OPPONENT_FEATURES = 11
AGGREGATE_FEATURES = 9
GEOMETRY_FEATURES = 5
OBS_SIZE = SELF_FEATURES + NEAREST_OPPONENT_SLOTS * PER_OPPONENT_FEATURES + AGGREGATE_FEATURES + GEOMETRY_FEATURES
assert OBS_SIZE == 84

QUEUE_FRAG_LIMIT = 20
RELOAD_TIMER_CAP = 60.0
ABILITY_COOLDOWN_CAP = 480.0
WALL_RAY_CAP = 40.0
FLOOR_RAY_CAP = 20.0
DIST_CAP = 100.0
ELEV_CAP = 20.0


def _clamp01(v):
    return max(0.0, min(1.0, v))


def _clamp_signed(v):
    return max(-1.0, min(1.0, v))


def build_observation(me: NetPlayer, peers: list, walls: Optional[list]) -> np.ndarray:
    """Builds the real, 84-feature observation vector for one player's current tick -- a direct
    Python port of apps2/emily-bot/observation.go's own buildObservation, field order matched
    exactly so a checkpoint trained here and a Go-side consumer would agree on feature meaning if
    one were ever built. `me` is this player's own current NetPlayer snapshot entity; `peers` is
    every other same-scene NetPlayer entity this tick; `walls` is the real (or None) level box
    list from fetch_queue_level_geometry()."""
    obs = np.zeros(OBS_SIZE, dtype=np.float32)
    i = 0

    # --- Self (26) ---
    obs[i] = _clamp01(me.health / 100.0); i += 1
    obs[i] = _clamp01(me.shield / 100.0); i += 1
    yaw_rad = me.yaw * np.pi / 180.0
    obs[i] = np.sin(yaw_rad); i += 1
    obs[i] = np.cos(yaw_rad); i += 1
    obs[i] = _clamp_signed(me.pitch / 90.0); i += 1
    weapon_onehot_start = i
    if 0 <= me.current_weapon < 8:
        obs[weapon_onehot_start + me.current_weapon] = 1.0
    i += 8
    max_ammo = WEAPON_MAX_AMMO.get(me.current_weapon, 0)
    obs[i] = _clamp01(me.ammo / max_ammo) if max_ammo > 0 else 0.0; i += 1
    obs[i] = 1.0 if me.is_shooting else 0.0; i += 1
    obs[i] = 1.0 if me.crouching else 0.0; i += 1
    obs[i] = 1.0 if me.in_vehicle else 0.0; i += 1
    obs[i] = 1.0 if me.state == STATE_ALIVE else 0.0; i += 1
    obs[i] = _clamp01(me.kills / QUEUE_FRAG_LIMIT); i += 1
    obs[i] = _clamp01(me.deaths / 10.0); i += 1
    obs[i] = 1.0 if me.hit_feedback else 0.0; i += 1
    obs[i] = _clamp01(me.storm_charges / 5.0); i += 1
    obs[i] = float(np.tanh(me.reward_feedback / 50.0)); i += 1
    obs[i] = _clamp01(me.reload_timer / RELOAD_TIMER_CAP); i += 1
    obs[i] = _clamp01(me.ability_cooldown / ABILITY_COOLDOWN_CAP); i += 1
    obs[i] = 1.0 if me.ability_cooldown == 0 else 0.0; i += 1
    assert i == SELF_FEATURES, i

    # --- Nearest-opponent block (4 x 11 = 44) ---
    visible = []
    for p in peers:
        if p.scene_id != me.scene_id or p.id == me.id:
            continue
        dx, dy, dz = p.x - me.x, p.y - me.y, p.z - me.z
        dist = float(np.sqrt(dx * dx + dy * dy + dz * dz))
        visible.append((dist, p, dx, dy, dz))
    visible.sort(key=lambda t: t[0])

    opp_start = i
    for slot in range(NEAREST_OPPONENT_SLOTS):
        base = opp_start + slot * PER_OPPONENT_FEATURES
        if slot < len(visible):
            dist, p, dx, dy, dz = visible[slot]
            bearing = (np.degrees(np.arctan2(dx, -dz)) - me.yaw)
            bearing_rad = np.radians(bearing)
            obs[base + 0] = 1.0
            obs[base + 1] = _clamp01(dist / DIST_CAP)
            obs[base + 2] = np.sin(bearing_rad)
            obs[base + 3] = np.cos(bearing_rad)
            obs[base + 4] = _clamp_signed(dy / ELEV_CAP)
            obs[base + 5] = _clamp01(p.health / 100.0)
            obs[base + 6] = WEAPON_LETHALITY.get(p.current_weapon, 0.0)
            obs[base + 7] = 1.0 if p.is_shooting else 0.0
            obs[base + 8] = 1.0 if p.is_bot else 0.0
            obs[base + 9] = 1.0 if p.state == STATE_ALIVE else 0.0
            obs[base + 10] = 1.0 if p.reload_timer > 0 else 0.0
        # else: stays zero-filled (Present=0)
    i = opp_start + NEAREST_OPPONENT_SLOTS * PER_OPPONENT_FEATURES

    # --- Aggregate/contextual (9) ---
    agg_start = i
    n_visible = len(visible)
    obs[agg_start + 0] = _clamp01(n_visible / 7.0)
    if n_visible > 0:
        obs[agg_start + 1] = obs[opp_start + 1]  # redundant w/ slot0 distance, always populated
        healths = [p.health for _, p, *_ in visible]
        obs[agg_start + 2] = _clamp01((sum(healths) / n_visible) / 100.0)
        obs[agg_start + 3] = _clamp01(sum(1 for _, p, *_ in visible if p.is_shooting) / 7.0)
        obs[agg_start + 4] = _clamp01(sum(1 for _, p, *_ in visible if p.health < 30) / 7.0)
        max_kills = max([me.kills] + [p.kills for _, p, *_ in visible])
        obs[agg_start + 6] = float(np.tanh((me.kills - max_kills) / 5.0))
    obs[agg_start + 5] = float(np.tanh(me.kills / (me.deaths + 1.0)))
    # obs[agg_start+7], obs[agg_start+8] -- team-architected placeholders (S459-46), always 0 in FFA
    i = agg_start + AGGREGATE_FEATURES

    # --- Geometry (5), real raycast -- S459-47 ---
    geo_start = i
    fwd_dx, fwd_dz = float(np.sin(yaw_rad)), float(-np.cos(yaw_rad))
    left_rad = (me.yaw - 90) * np.pi / 180.0
    right_rad = (me.yaw + 90) * np.pi / 180.0
    back_rad = (me.yaw + 180) * np.pi / 180.0
    if walls:
        obs[geo_start + 0] = _clamp01(raycast(walls, me.x, me.y, me.z, fwd_dx, 0, fwd_dz, WALL_RAY_CAP) / WALL_RAY_CAP)
        obs[geo_start + 1] = _clamp01(raycast(walls, me.x, me.y, me.z, float(np.sin(left_rad)), 0, float(-np.cos(left_rad)), WALL_RAY_CAP) / WALL_RAY_CAP)
        obs[geo_start + 2] = _clamp01(raycast(walls, me.x, me.y, me.z, float(np.sin(right_rad)), 0, float(-np.cos(right_rad)), WALL_RAY_CAP) / WALL_RAY_CAP)
        obs[geo_start + 3] = _clamp01(raycast(walls, me.x, me.y, me.z, float(np.sin(back_rad)), 0, float(-np.cos(back_rad)), WALL_RAY_CAP) / WALL_RAY_CAP)
        obs[geo_start + 4] = _clamp01(raycast(walls, me.x, me.y, me.z, 0, -1, 0, FLOOR_RAY_CAP) / FLOOR_RAY_CAP)
    # else: stays real, honest 0 -- see this module's own doc comment / geometry.go's precedent

    return obs


# --- Reward (mirrors apps2/emily-bot/reward.go's 4-tier design exactly) ---

REWARD_FEEDBACK_SCALE = 1.0 / 50.0
REWARD_DEATH = -1.0
LOW_HEALTH_THRESHOLD_FRAC = 0.30
REWARD_LOW_HEALTH_ENGAGED_PER_TICK = -0.02
REWARD_LOW_HEALTH_DISENGAGED_PER_TICK = 0.01
REWARD_ALIVE_PER_TICK = 0.001
REWARD_APPROACH_ADVANTAGED_TARGET_PER_TICK = 0.01


@dataclass
class RewardSnapshot:
    health: float
    reward_feedback: float
    deaths: int
    nearest_enemy_dist: float  # 0 = none visible
    nearest_enemy_health_frac: float  # 0 if none visible


def compute_reward(prev: RewardSnapshot, cur: RewardSnapshot) -> float:
    """4-tier reward, a direct Python port of apps2/emily-bot/reward.go's own computeReward.
    Team term omitted entirely here (not just zeroed) -- FFA-only per the founder's own explicit
    S459-46 instruction; see reward.go's own TeamRewardContext doc comment for the real,
    architected-not-built team design this would extend into."""
    reward = 0.0
    reward += cur.reward_feedback * REWARD_FEEDBACK_SCALE
    if cur.deaths > prev.deaths:
        reward += REWARD_DEATH * (cur.deaths - prev.deaths)

    if cur.health / 100.0 < LOW_HEALTH_THRESHOLD_FRAC and cur.nearest_enemy_dist > 0:
        if cur.nearest_enemy_dist > prev.nearest_enemy_dist:
            reward += REWARD_LOW_HEALTH_DISENGAGED_PER_TICK
        else:
            reward += REWARD_LOW_HEALTH_ENGAGED_PER_TICK

    if cur.health > 0:
        reward += REWARD_ALIVE_PER_TICK

    if (cur.nearest_enemy_dist > 0 and cur.nearest_enemy_dist < prev.nearest_enemy_dist and
            cur.nearest_enemy_health_frac > 0 and cur.nearest_enemy_health_frac < cur.health / 100.0):
        reward += REWARD_APPROACH_ADVANTAGED_TARGET_PER_TICK

    return reward


def _reward_snapshot_from(me: NetPlayer, peers: list) -> RewardSnapshot:
    nearest_dist, nearest_health = 0.0, 0.0
    best = None
    for p in peers:
        if p.scene_id != me.scene_id or p.id == me.id:
            continue
        dx, dy, dz = p.x - me.x, p.y - me.y, p.z - me.z
        d = float(np.sqrt(dx * dx + dy * dy + dz * dz))
        if best is None or d < best:
            best = d
            nearest_dist, nearest_health = d, p.health / 100.0
    return RewardSnapshot(
        health=me.health, reward_feedback=me.reward_feedback, deaths=me.deaths,
        nearest_enemy_dist=nearest_dist, nearest_enemy_health_frac=nearest_health,
    )


# --- Action encoding ---

ACTION_SIZE = 7  # fwd, strafe, yaw_delta, pitch_delta, attack, jump, weapon_select
YAW_DELTA_SCALE = 6.0   # degrees/tick at max stick deflection -- real, hand-tuned to match a human turn rate
PITCH_DELTA_SCALE = 4.0
ACTIVITY_THRESHOLD = 0.5


def decode_action(action: np.ndarray, cur_yaw: float, cur_pitch: float):
    """Maps a flat 7-float action (gymnasium's own Box convention, matching BRAWLPIT's own
    flat-Box precedent) to the real UserCmd fields the server expects. yaw/pitch are DELTAS
    applied to the current aim (the real client itself works this way -- apps/lobby/src/main.c's
    own mouse-look accumulates a delta each frame, it never sends an absolute target)."""
    fwd = float(np.clip(action[0], -1, 1))
    strafe = float(np.clip(action[1], -1, 1))
    yaw = cur_yaw + float(np.clip(action[2], -1, 1)) * YAW_DELTA_SCALE
    pitch = float(np.clip(cur_pitch + float(np.clip(action[3], -1, 1)) * PITCH_DELTA_SCALE, -89, 89))
    buttons = 0
    if action[4] > ACTIVITY_THRESHOLD:
        buttons |= BTN_ATTACK
    if action[5] > ACTIVITY_THRESHOLD:
        buttons |= BTN_JUMP
    weapon_idx = int(np.clip((action[6] + 1) / 2 * 7, 0, 7))
    return fwd, strafe, yaw, pitch, buttons, weapon_idx


# --- gymnasium Env ---

if gym is not None:

    class ShankpitQueueEnv(gym.Env):
        """A real gymnasium environment for SHANKPIT MODE_QUEUE, speaking the actual wire
        protocol against a real (ideally --fast-forward'd) bin/shank_server instance. One episode
        = one life (see this module's own doc comment for why QUEUE's continuous-respawn design
        makes this the natural boundary instead of BRAWLPIT's own per-match terminal state)."""

        metadata = {"render_modes": []}

        def __init__(self, host: str = "127.0.0.1", port: int = 6969, max_episode_steps: int = 3000):
            super().__init__()
            self.host = host
            self.port = port
            self.max_episode_steps = max_episode_steps
            self.observation_space = spaces.Box(low=-2.0, high=2.0, shape=(OBS_SIZE,), dtype=np.float32)
            self.action_space = spaces.Box(low=-1.0, high=1.0, shape=(ACTION_SIZE,), dtype=np.float32)
            self.client: Optional[PacketClient] = None
            self.walls = None
            self._steps_this_episode = 0
            self._prev_reward_snap: Optional[RewardSnapshot] = None
            self._cur_yaw = 0.0
            self._cur_pitch = 0.0

        def _connect_if_needed(self):
            if self.client is not None:
                return
            self.client = PacketClient(self.host, self.port)
            if not self.client.connect():
                raise ConnectionError(f"failed to connect to shank_server at {self.host}:{self.port}")
            self.walls = fetch_queue_level_geometry()

        def _find_me(self, entities):
            for e in entities:
                if e.id == self.client.client_id:
                    return e
            return None

        def _wait_for_alive_snapshot(self, timeout_s: float = 30.0):
            """Real, found-live fix: the server only marks a connected slot's player `active`
            once it has received at least one real UserCmd (cmd_seen) -- a client that only
            listens after connect(), never sends, never appears in any snapshot at all. Keeps
            sending a real no-op action every loop iteration (matching how both the actual C
            client and apps2/emily-bot's own tick loop behave -- neither ever waits passively for
            a snapshot before sending) until a snapshot shows this client alive."""
            deadline = time.time() + timeout_s
            while time.time() < deadline:
                self.client.send_action(0.0, 0.0, self._cur_yaw, self._cur_pitch, 0, WPN_MAGNUM)
                entities = self.client.recv_snapshot()
                if not entities:
                    continue
                me = self._find_me(entities)
                if me is not None and me.state == STATE_ALIVE:
                    return me, entities
            raise TimeoutError("no live respawn snapshot within timeout")

        def reset(self, *, seed=None, options=None):
            super().reset(seed=seed)
            self._connect_if_needed()
            me, entities = self._wait_for_alive_snapshot()
            self._steps_this_episode = 0
            self._cur_yaw, self._cur_pitch = me.yaw, me.pitch
            self._prev_reward_snap = _reward_snapshot_from(me, entities)
            obs = build_observation(me, entities, self.walls)
            return obs, {}

        def step(self, action: np.ndarray):
            fwd, strafe, yaw, pitch, buttons, weapon_idx = decode_action(action, self._cur_yaw, self._cur_pitch)
            self.client.send_action(fwd, strafe, yaw, pitch, buttons, weapon_idx)
            self._cur_yaw, self._cur_pitch = yaw, pitch

            entities = self.client.recv_snapshot()
            self._steps_this_episode += 1
            if not entities:
                # A real, honest dropped-tick degrade -- repeat the last known reward-relevant
                # state rather than crash a whole training run over one lost UDP packet.
                return (np.zeros(OBS_SIZE, dtype=np.float32), 0.0, False,
                        self._steps_this_episode >= self.max_episode_steps, {})

            me = self._find_me(entities)
            if me is None:
                return (np.zeros(OBS_SIZE, dtype=np.float32), 0.0, False,
                        self._steps_this_episode >= self.max_episode_steps, {})

            cur_snap = _reward_snapshot_from(me, entities)
            reward = compute_reward(self._prev_reward_snap, cur_snap)
            self._prev_reward_snap = cur_snap

            terminated = me.state == STATE_DEAD
            truncated = self._steps_this_episode >= self.max_episode_steps
            obs = build_observation(me, entities, self.walls)
            return obs, reward, terminated, truncated, {"kills": me.kills, "deaths": me.deaths}

        def close(self):
            if self.client is not None:
                self.client.close()
                self.client = None

else:  # pragma: no cover
    ShankpitQueueEnv = None


def _smoke_test(host: str, port: int, steps: int):
    """Real, live smoke test -- connects a real client to a real, running shank_server, steps it
    for real ticks with a trivial random policy, and reports observation/reward sanity. Not a
    unit test (needs a live server) -- run manually, mirrors BRAWLPIT's own _smoke_test."""
    if ShankpitQueueEnv is None:
        print("gymnasium not installed -- cannot run the smoke test")
        return 1
    env = ShankpitQueueEnv(host=host, port=port, max_episode_steps=steps)
    obs, info = env.reset()
    print(f"[smoke] reset ok, obs shape={obs.shape} obs[:5]={obs[:5]}")
    total_reward = 0.0
    for t in range(steps):
        action = env.action_space.sample()
        obs, reward, terminated, truncated, info = env.step(action)
        total_reward += reward
        if t % 50 == 0:
            print(f"[smoke] t={t} reward={reward:.4f} total={total_reward:.4f} terminated={terminated} info={info}")
        if terminated or truncated:
            print(f"[smoke] episode ended at t={t} (terminated={terminated} truncated={truncated}), resetting")
            obs, info = env.reset()
    env.close()
    print(f"[smoke] done. total_reward={total_reward:.4f} over {steps} steps")
    return 0


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="SHANKPIT packet-level RL env smoke test")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=6969)
    parser.add_argument("--steps", type=int, default=200)
    args = parser.parse_args()
    sys.exit(_smoke_test(args.host, args.port, args.steps))
