#!/usr/bin/env python3
"""
scripts/frozen_policy_bot.py (S459-54) -- a real, standalone self-play opponent: connects to a
running shank_server as a normal QUEUE player, but every action comes from a real, FROZEN
(no-gradient-update) PPO checkpoint's own policy.predict(), not a heuristic or a learning model.

Real, necessary primitive BRAWLPIT never needed: BRAWLPIT's own rl_train_packet.py gets self-play
"for free" because its packet env already drives BOTH sides of one match from a single Python
process (own_state + opp_state in one observation) and can swap in a frozen model in-process
(BrawlpitPacketEnv's own opponent_checkpoint_path). SHANKPIT's server is a real, continuous, live
world where each UDP connection is exactly one independent player (S459-44/45's own real find) --
there is no "the other player" slot inside one process to swap a model into. The real, correct
analog here is a SEPARATE OS process that connects as its own real player and runs the frozen
policy's own inference loop, exactly the way `apps2/emily-bot` runs a heuristic loop -- just with
a PPO forward pass in place of the heuristic. rl_train_packet.py (S459-54) launches one of these
per self-play opponent, alongside or instead of real emily-bot heuristic bots.

Deliberately NOT trained here -- model.predict(obs, deterministic=False) samples from the frozen
policy's own action distribution (matches BRAWLPIT's own frozen-opponent inference convention,
real stochasticity rather than a deterministic argmax so the opponent doesn't play a single,
exploitable fixed line every match) but never calls model.learn() or saves anything back.
"""

import argparse
import json
import os
import sys
import time

from rl_env_packet import (
    PacketClient, build_observation, decode_action, fetch_queue_level_geometry,
    STATE_ALIVE, WPN_MAGNUM,
)

try:
    from stable_baselines3 import PPO
except ImportError:
    PPO = None


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, required=True)
    p.add_argument("--checkpoint", required=True, help="path to a real, saved PPO .zip checkpoint")
    p.add_argument("--session-duration", type=float, default=1e9, help="real seconds to run before exiting (default: effectively forever)")
    p.add_argument("--report-kills-to", default=None,
                   help="real, minimal evaluation-match support (S459-54): writes {\"kills\": N, \"deaths\": N} "
                        "to this path when the session ends, so an orchestrator running two of these against "
                        "each other can determine a winner without a third observer connection.")
    p.add_argument("--report-interval", type=float, default=1.5,
                   help="S474 follow-up (founder real-time: 'first to 1 may not be as good as first to 5' -- "
                        "an orchestrator wants to detect a decisive early result, not just read a final "
                        "count once the whole session ends). Also overwrites --report-kills-to's own file "
                        "at this real interval throughout the session (not just once at the end), so an "
                        "orchestrator polling it can end an evaluation match the moment one side reaches a "
                        "real kill target, instead of always waiting out the full --session-duration.")
    args = p.parse_args()

    if PPO is None:
        print("stable_baselines3 not installed -- cannot run a frozen-policy bot.")
        return 1

    model = PPO.load(args.checkpoint, device="cpu")
    print(f"[frozen-policy-bot] loaded {args.checkpoint}, connecting to {args.host}:{args.port}")

    client = PacketClient(args.host, args.port)
    if not client.connect():
        print(f"[frozen-policy-bot] failed to connect to {args.host}:{args.port}")
        return 1
    print(f"[frozen-policy-bot] connected as client_id={client.client_id}")

    walls = fetch_queue_level_geometry()
    cur_yaw, cur_pitch = 0.0, 0.0
    start = time.time()

    # Real, honest wait-for-spawn -- same convention ShankpitQueueEnv._wait_for_alive_snapshot
    # already establishes (S459-48's own real, found-live fix: the server only marks a slot
    # `active` once it's received a real UserCmd, so a client that only listens after connect()
    # never appears in any snapshot at all).
    me = None
    while me is None and time.time() - start < args.session_duration:
        client.send_action(0.0, 0.0, cur_yaw, cur_pitch, 0, WPN_MAGNUM)
        entities = client.recv_snapshot()
        if not entities:
            continue
        candidate = next((e for e in entities if e.id == client.client_id), None)
        if candidate is not None and candidate.state == STATE_ALIVE:
            me = candidate

    last_report_at = 0.0

    def _write_report():
        if not args.report_kills_to:
            return
        k = int(me.kills) if me is not None else 0
        d = int(me.deaths) if me is not None else 0
        tmp = args.report_kills_to + f".tmp{os.getpid()}"
        with open(tmp, "w") as f:
            json.dump({"kills": k, "deaths": d}, f)
        os.replace(tmp, args.report_kills_to)  # atomic, matching rl_league.py's own established convention -- a poller never reads a half-written file

    while time.time() - start < args.session_duration:
        if me is None:
            break
        if args.report_interval > 0 and time.time() - last_report_at >= args.report_interval:
            _write_report()
            last_report_at = time.time()
        obs = build_observation(me, entities, walls)
        action, _ = model.predict(obs, deterministic=False)
        fwd, strafe, cur_yaw, cur_pitch, buttons, weapon_idx = decode_action(action, cur_yaw, cur_pitch)
        client.send_action(fwd, strafe, cur_yaw, cur_pitch, buttons, weapon_idx)

        # Real, live-found bug (2026-09-17, founder real-time: "no bots in my game"): a dropped
        # or not-yet-ready UDP snapshot is normal and frequent, not an error -- but reassigning
        # `entities` unconditionally before this guard left it as None on that tick, and the
        # NEXT loop iteration's build_observation call above used that now-None value as
        # `peers`, crashing with "TypeError: 'NoneType' object is not iterable" and killing the
        # whole bot process. Fix: only replace `entities`/`me` when a snapshot actually arrived,
        # so a missed packet just keeps using the last known-good state instead of nulling it.
        new_entities = client.recv_snapshot()
        if not new_entities:
            continue
        entities = new_entities
        me = next((e for e in entities if e.id == client.client_id), None)

    client.close()
    if args.report_kills_to:
        _write_report()
        final_kills = int(me.kills) if me is not None else 0
        final_deaths = int(me.deaths) if me is not None else 0
        print(f"[frozen-policy-bot] reported kills={final_kills} deaths={final_deaths} -> {args.report_kills_to}")
    print("[frozen-policy-bot] session ended")
    return 0


if __name__ == "__main__":
    sys.exit(main())
