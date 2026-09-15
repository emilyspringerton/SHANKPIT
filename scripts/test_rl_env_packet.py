#!/usr/bin/env python3
"""Real, network-free unit tests for rl_env_packet.py (S459-48). Mirrors BRAWLPIT's own
scripts/test_rl_env_packet.py conventions (plain unittest, no pytest dependency)."""

import ctypes
import unittest

import numpy as np

from rl_env_packet import (
    NetHeader, UserCmd, NetPlayer,
    encode_connect, decode_welcome, encode_usercmd, decode_snapshot,
    build_observation, compute_reward, RewardSnapshot, OBS_SIZE,
    Wall, raycast, _slab, decode_action, _fibonacci, SURVIVAL_STREAK_FIB_CAP,
    WPN_MAGNUM, WPN_SNIPER, STATE_ALIVE, STATE_DEAD,
)


class TestStructSizes(unittest.TestCase):
    def test_net_header_is_12_bytes(self):
        self.assertEqual(ctypes.sizeof(NetHeader), 12)

    def test_user_cmd_is_36_bytes(self):
        self.assertEqual(ctypes.sizeof(UserCmd), 36)

    def test_net_player_is_84_bytes(self):
        self.assertEqual(ctypes.sizeof(NetPlayer), 84)  # S459-69: grew from 72 when vx/vy/vz were added

    def test_net_player_field_offsets_match_the_real_compiled_c_struct(self):
        # Real offsets verified via a compiled sizeof/offsetof C probe against protocol.h during
        # S459-44/45/47 -- re-asserted here so a future protocol.h change that breaks the byte
        # layout fails loudly here too, not just in the Go side's own snapshot_test.go.
        expected = {
            "id": 0, "scene_id": 1, "is_bot": 2, "team_id": 3, "last_seq": 4,
            "x": 8, "y": 12, "z": 16, "yaw": 20, "pitch": 24,
            "current_weapon": 28, "state": 29, "health": 30, "shield": 31,
            "is_shooting": 32, "crouching": 33, "reward_feedback": 36,
            "ammo": 40, "in_vehicle": 41, "carried_flag_team_id": 42, "hit_feedback": 43,
            "storm_charges": 44, "kills": 46, "deaths": 48,
            "death_elapsed_ms": 50, "death_duration_ms": 52,
            "death_dir_x": 56, "death_dir_z": 60,
            "reload_timer": 64, "ability_cooldown": 66, "kill_streak": 68,
            "vx": 72, "vy": 76, "vz": 80,  # S459-69
        }
        for field, off in expected.items():
            self.assertEqual(getattr(NetPlayer, field).offset, off, f"field {field}")


class TestConnectHandshake(unittest.TestCase):
    def test_encode_connect_shape(self):
        buf = encode_connect(game_mode=108)
        self.assertEqual(len(buf), 13 + 256)
        self.assertEqual(buf[0], 0)  # PACKET_CONNECT
        self.assertEqual(buf[12], 108)

    def test_decode_welcome_extracts_client_id_and_scene(self):
        data = bytearray(13)
        data[0] = 3  # PACKET_WELCOME
        data[1] = 7
        data[9] = 9
        result = decode_welcome(bytes(data))
        self.assertEqual(result, (7, 9))

    def test_decode_welcome_rejects_short_buffer(self):
        self.assertIsNone(decode_welcome(b"\x03\x01"))


class TestUsercmdWireLayout(unittest.TestCase):
    def test_encode_usercmd_places_the_real_struct_at_offset_13(self):
        cmd = UserCmd(sequence=42, timestamp=1000, msec=16, fwd=1.0, str_=-0.5,
                      yaw=90.0, pitch=0.0, buttons=2, weapon_idx=4)
        buf = encode_usercmd(cmd)
        self.assertEqual(len(buf), 13 + 36)
        self.assertEqual(buf[0], 1)  # PACKET_USERCMD
        self.assertEqual(buf[12], 1)  # count=1
        roundtrip = UserCmd.from_buffer_copy(buf, 13)
        self.assertEqual(roundtrip.sequence, 42)
        self.assertAlmostEqual(roundtrip.fwd, 1.0)
        self.assertAlmostEqual(roundtrip.yaw, 90.0)
        self.assertEqual(roundtrip.weapon_idx, 4)


class TestSnapshotWireLayout(unittest.TestCase):
    def _build_snapshot(self, entities):
        header_size = 12
        entity_size = ctypes.sizeof(NetPlayer)
        buf = bytearray(header_size + 1 + entity_size * len(entities))
        buf[0] = 2  # PACKET_SNAPSHOT
        buf[8] = len(entities)  # entity_count
        off = header_size + 1
        for e in entities:
            buf[off:off + entity_size] = bytes(e)
            off += entity_size
        return bytes(buf)

    def test_decodes_multiple_players_in_order(self):
        p1 = NetPlayer(id=1, scene_id=9, health=100, x=1.0, y=2.0, z=3.0)
        p2 = NetPlayer(id=2, scene_id=9, health=42, x=5.0, y=6.0, z=7.0)
        data = self._build_snapshot([p1, p2])
        decoded = decode_snapshot(data)
        self.assertEqual(len(decoded), 2)
        self.assertEqual(decoded[0].id, 1)
        self.assertEqual(decoded[0].health, 100)
        self.assertEqual(decoded[1].id, 2)
        self.assertEqual(decoded[1].health, 42)

    def test_rejects_non_snapshot_packet(self):
        self.assertIsNone(decode_snapshot(b"\x03" + b"\x00" * 20))

    def test_truncated_packet_returns_only_complete_entities(self):
        p1 = NetPlayer(id=1, scene_id=9, health=100)
        p2 = NetPlayer(id=2, scene_id=9, health=50)
        data = self._build_snapshot([p1, p2])
        truncated = data[:12 + 1 + ctypes.sizeof(NetPlayer)]  # only room for the first entity
        decoded = decode_snapshot(truncated)
        self.assertEqual(len(decoded), 1)
        self.assertEqual(decoded[0].id, 1)


class TestBuildObservation(unittest.TestCase):
    def test_shape_matches_obs_size(self):
        me = NetPlayer(id=1, scene_id=9, health=100, shield=100, state=STATE_ALIVE)
        obs = build_observation(me, [], None)
        self.assertEqual(obs.shape, (OBS_SIZE,))
        self.assertEqual(OBS_SIZE, 84)

    def test_self_health_and_shield_fractions(self):
        me = NetPlayer(id=1, scene_id=9, health=50, shield=25, state=STATE_ALIVE)
        obs = build_observation(me, [], None)
        self.assertAlmostEqual(obs[0], 0.5)
        self.assertAlmostEqual(obs[1], 0.25)

    def test_no_peers_zero_fills_opponent_block(self):
        me = NetPlayer(id=1, scene_id=9, health=100, state=STATE_ALIVE)
        obs = build_observation(me, [], None)
        opp_start = 26
        # Present flag for every one of the 4 slots should be 0.
        for slot in range(4):
            self.assertEqual(obs[opp_start + slot * 11], 0.0)

    def test_nearest_opponent_ordering_and_fields(self):
        me = NetPlayer(id=1, scene_id=9, x=0, y=0, z=0, yaw=0, health=100, state=STATE_ALIVE)
        far = NetPlayer(id=2, scene_id=9, x=50, y=0, z=0, health=80, state=STATE_ALIVE)
        near = NetPlayer(id=3, scene_id=9, x=5, y=0, z=0, health=60, state=STATE_ALIVE)
        other_scene = NetPlayer(id=4, scene_id=1, x=1, y=0, z=0, health=10, state=STATE_ALIVE)
        obs = build_observation(me, [far, near, other_scene], None)
        opp_start = 26
        self.assertEqual(obs[opp_start + 0], 1.0)  # slot 0 present
        self.assertAlmostEqual(obs[opp_start + 5], 0.6)  # nearest (id=3, health=60) health frac

    def test_dual_cooldown_features(self):
        # Real scenario: sniper storm activated (5 charges + shared cooldown started), no shots
        # fired yet -- storm charges stay banked at max even while the shared cooldown counts
        # down (mirrors apps2/emily-bot's own TestSelfDualCooldownFeatures, S459-47).
        me = NetPlayer(id=1, scene_id=9, health=100, state=STATE_ALIVE,
                        storm_charges=5, ability_cooldown=480, current_weapon=WPN_SNIPER)
        obs = build_observation(me, [], None)
        # Self-block field order (build_observation): 0 health, 1 shield, 2 yaw_sin, 3 yaw_cos,
        # 4 pitch, 5-12 weapon_onehot[8], 13 ammo, 14 is_shooting, 15 crouching, 16 in_vehicle,
        # 17 alive, 18 kills, 19 deaths, 20 hit_feedback, 21 storm_charges, 22 reward_feedback,
        # 23 reload, 24 ability_cooldown, 25 ability_ready.
        self.assertAlmostEqual(obs[21], 1.0)  # SelfStormChargesFrac
        self.assertEqual(obs[25], 0.0)  # SelfAbilityReady -- NOT ready, cooldown still counting
        self.assertAlmostEqual(obs[24], 1.0)  # SelfAbilityCooldownFrac at max (480/480)

    def test_ability_ready_when_cooldown_is_zero(self):
        me = NetPlayer(id=1, scene_id=9, health=100, state=STATE_ALIVE, ability_cooldown=0)
        obs = build_observation(me, [], None)
        self.assertEqual(obs[25], 1.0)  # SelfAbilityReady


class TestRaycast(unittest.TestCase):
    def test_hits_a_wall_ahead(self):
        walls = [Wall(x=0, y=0, z=-10, sx=4, sy=4, sz=1)]
        d = raycast(walls, 0, 0, 0, 0, 0, -1, 40)
        self.assertGreater(d, 9.0)
        self.assertLess(d, 10.0)

    def test_misses_when_pointed_away(self):
        walls = [Wall(x=0, y=0, z=-10, sx=4, sy=4, sz=1)]
        d = raycast(walls, 0, 0, 0, 0, 0, 1, 40)
        self.assertEqual(d, 40)

    def test_no_walls_returns_cap(self):
        self.assertEqual(raycast([], 0, 0, 0, 0, 0, -1, 40), 40)

    def test_matches_slab_reference_on_random_cases(self):
        # S459-73: raycast() now inlines _slab's own per-axis math directly (real, measured perf
        # fix -- see this file's own module doc comment / CHANGELOG for the cProfile numbers).
        # This locks in that the inlined version stays numerically identical to _slab, the real,
        # standalone reference implementation still tested directly above -- a real regression
        # guard, not just a smoke test, matching the 200k-random-trial equivalence check this fix
        # was actually verified with before shipping.
        import random
        rng = random.Random(1234)
        for _ in range(2000):
            walls = [
                Wall(rng.uniform(-50, 50), rng.uniform(-10, 10), rng.uniform(-50, 50),
                     rng.uniform(1, 20), rng.uniform(1, 20), rng.uniform(1, 20))
                for _ in range(rng.randint(0, 5))
            ]
            ox, oy, oz = rng.uniform(-30, 30), rng.uniform(-5, 5), rng.uniform(-30, 30)
            dx, dy, dz = rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1)
            cap = rng.uniform(10, 100)

            got = raycast(walls, ox, oy, oz, dx, dy, dz, cap)

            # Real, standalone reference: the exact pre-S459-73 algorithm, calling _slab per axis.
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

            self.assertAlmostEqual(got, best, places=9)


class TestComputeReward(unittest.TestCase):
    def test_kill_scores_positive(self):
        prev = RewardSnapshot(health=100, reward_feedback=0, deaths=0,
                               nearest_enemy_dist=0, nearest_enemy_health_frac=0)
        cur = RewardSnapshot(health=100, reward_feedback=150.0, deaths=0,
                              nearest_enemy_dist=0, nearest_enemy_health_frac=0)
        self.assertGreater(compute_reward(prev, cur), 0)

    def test_death_scores_negative(self):
        prev = RewardSnapshot(health=20, reward_feedback=0, deaths=0,
                               nearest_enemy_dist=0, nearest_enemy_health_frac=0)
        cur = RewardSnapshot(health=0, reward_feedback=0, deaths=1,
                              nearest_enemy_dist=0, nearest_enemy_health_frac=0)
        self.assertLess(compute_reward(prev, cur), 0)

    def test_disengaging_while_low_health_beats_engaging(self):
        prev = RewardSnapshot(health=25, reward_feedback=0, deaths=0,
                               nearest_enemy_dist=5, nearest_enemy_health_frac=0.5)
        engaged = RewardSnapshot(health=25, reward_feedback=0, deaths=0,
                                  nearest_enemy_dist=4, nearest_enemy_health_frac=0.5)
        disengaged = RewardSnapshot(health=25, reward_feedback=0, deaths=0,
                                     nearest_enemy_dist=10, nearest_enemy_health_frac=0.5)
        self.assertGreater(compute_reward(prev, disengaged), compute_reward(prev, engaged))

    def test_survival_streak_none_is_a_real_noop(self):
        prev = RewardSnapshot(health=100, reward_feedback=0, deaths=0, nearest_enemy_dist=0, nearest_enemy_health_frac=0)
        cur = RewardSnapshot(health=100, reward_feedback=0, deaths=0, nearest_enemy_dist=0, nearest_enemy_health_frac=0)
        # No survival_ticks given -- reward should equal just the flat REWARD_ALIVE_PER_TICK tier.
        from rl_env_packet import REWARD_ALIVE_PER_TICK
        self.assertAlmostEqual(compute_reward(prev, cur), REWARD_ALIVE_PER_TICK)

    def test_survival_streak_grows_with_ticks(self):
        prev = RewardSnapshot(health=100, reward_feedback=0, deaths=0, nearest_enemy_dist=0, nearest_enemy_health_frac=0)
        cur = RewardSnapshot(health=100, reward_feedback=0, deaths=0, nearest_enemy_dist=0, nearest_enemy_health_frac=0)
        early = compute_reward(prev, cur, survival_ticks=2)
        later = compute_reward(prev, cur, survival_ticks=8)
        self.assertGreater(later, early)

    def test_survival_streak_scales_with_low_health(self):
        prev = RewardSnapshot(health=100, reward_feedback=0, deaths=0, nearest_enemy_dist=0, nearest_enemy_health_frac=0)
        full_health = RewardSnapshot(health=100, reward_feedback=0, deaths=0, nearest_enemy_dist=0, nearest_enemy_health_frac=0)
        low_health = RewardSnapshot(health=1, reward_feedback=0, deaths=0, nearest_enemy_dist=0, nearest_enemy_health_frac=0)
        r_full = compute_reward(prev, full_health, survival_ticks=5)
        r_low = compute_reward(prev, low_health, survival_ticks=5)
        self.assertGreater(r_low, r_full)

    def test_survival_streak_never_pays_on_the_death_tick(self):
        prev = RewardSnapshot(health=20, reward_feedback=0, deaths=0, nearest_enemy_dist=0, nearest_enemy_health_frac=0)
        died = RewardSnapshot(health=0, reward_feedback=0, deaths=1, nearest_enemy_dist=0, nearest_enemy_health_frac=0)
        # Even with a real, positive (stale) survival_ticks, the death-tick guard must refuse to pay tier 5.
        with_streak = compute_reward(prev, died, survival_ticks=10)
        without_streak = compute_reward(prev, died, survival_ticks=None)
        self.assertAlmostEqual(with_streak, without_streak)

    def test_survival_streak_stops_paying_past_the_cap_not_just_clamps(self):
        # Real, found-live bug this port ships already-fixed (see the module doc comment): the
        # term must stop applying entirely once survival_ticks exceeds the cap, not keep paying
        # fib(cap) forever.
        prev = RewardSnapshot(health=100, reward_feedback=0, deaths=0, nearest_enemy_dist=0, nearest_enemy_health_frac=0)
        cur = RewardSnapshot(health=100, reward_feedback=0, deaths=0, nearest_enemy_dist=0, nearest_enemy_health_frac=0)
        at_cap = compute_reward(prev, cur, survival_ticks=SURVIVAL_STREAK_FIB_CAP)
        past_cap = compute_reward(prev, cur, survival_ticks=SURVIVAL_STREAK_FIB_CAP + 1)
        from rl_env_packet import REWARD_ALIVE_PER_TICK
        self.assertGreater(at_cap, REWARD_ALIVE_PER_TICK)
        self.assertAlmostEqual(past_cap, REWARD_ALIVE_PER_TICK)  # tier 5 contributes nothing past the cap


class TestFibonacci(unittest.TestCase):
    def test_matches_the_real_standard_sequence(self):
        self.assertEqual([_fibonacci(n) for n in range(1, 8)], [1, 1, 2, 3, 5, 8, 13])

    def test_zero_and_negative_are_zero(self):
        self.assertEqual(_fibonacci(0), 0)
        self.assertEqual(_fibonacci(-5), 0)


class TestDecodeAction(unittest.TestCase):
    def test_weapon_select_maps_full_range(self):
        _, _, _, _, _, w_low = decode_action(np.array([0, 0, 0, 0, -1, -1, -1], dtype=np.float32), 0, 0)
        _, _, _, _, _, w_high = decode_action(np.array([0, 0, 0, 0, -1, -1, 1], dtype=np.float32), 0, 0)
        self.assertEqual(w_low, 0)
        self.assertEqual(w_high, 7)

    def test_attack_button_threshold(self):
        from rl_env_packet import BTN_ATTACK
        fwd, strafe, yaw, pitch, buttons_on, _ = decode_action(
            np.array([0, 0, 0, 0, 0.9, 0, 0], dtype=np.float32), 0, 0)
        _, _, _, _, buttons_off, _ = decode_action(
            np.array([0, 0, 0, 0, 0.1, 0, 0], dtype=np.float32), 0, 0)
        self.assertTrue(buttons_on & BTN_ATTACK)
        self.assertFalse(buttons_off & BTN_ATTACK)

    def test_yaw_delta_applies_relative_to_current_yaw(self):
        _, _, yaw, _, _, _ = decode_action(np.array([0, 0, 1.0, 0, 0, 0, 0], dtype=np.float32), cur_yaw=45.0, cur_pitch=0.0)
        self.assertGreater(yaw, 45.0)


if __name__ == "__main__":
    unittest.main()
