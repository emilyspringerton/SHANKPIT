#!/usr/bin/env python3
"""rl_train_packet.py -- SHANKPIT's real packet-level PPO training script (S459-48).

Founder real-time: "continue adding stuff to make our bot training pipeline real and work." This
is the real training loop rl_env_packet.py's own module doc comment names as the single biggest
remaining gap -- BRAWLPIT's own equivalent (rl_train_packet.py) is honestly documented there as
"real, honest, not done" even for BRAWLPIT itself (see BRAWLPIT/docs/RL_TRAINING_NORTHSTAR.md).
SHANKPIT gets there first: stable_baselines3 and gymnasium are BOTH real, importable, installed
in this sandbox right now (checked directly, not assumed -- a real, current change from an
earlier session's own claim that they weren't installable).

Real, honest scope for this first pass: a single PPO policy, trained via real self-play against
the real, standing packet-level bot pool (apps2/emily-bot, the exact same bots
shankpit-bot-pool.service runs live) rather than a full 3-archetype AlphaStar-style league --
scripts/rl_league.py's own register_generation_snapshot() needs 3 real, DISTINCT checkpoints
(MAIN/MAIN_EXPLOITER/LEAGUE_EXPLOITER) to mean anything; duplicating one checkpoint into all 3
roles would be a fabricated, not real, archetype diversity, so registration is deliberately left
for a later pass once real distinct training runs exist. This script's own real job is proving
the training loop itself works end to end: connect -> collect real rollouts against real opponents
-> update a real policy -> save a real checkpoint file SB3's own PPO.load can read back.
"""

import argparse
import os
import subprocess
import sys
import time

from rl_env_packet import ShankpitQueueEnv, gym


def launch_server(binary: str, port: int, fast_forward: bool) -> subprocess.Popen:
    args = [binary, "--deathmatch", "--port", str(port)]
    if fast_forward:
        args.append("--fast-forward")
    return subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def launch_opponent_bots(binary: str, host: str, port: int, count: int, session_duration: str) -> list:
    procs = []
    for _ in range(count):
        procs.append(subprocess.Popen(
            [binary, "-host", host, "-port", str(port), "-mode", "108",
             "-no-report", "-session-duration", session_duration],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        ))
    return procs


def main():
    parser = argparse.ArgumentParser(description="SHANKPIT real packet-level PPO training run")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=17777)
    parser.add_argument("--timesteps", type=int, default=20000)
    parser.add_argument("--max-episode-steps", type=int, default=1000)
    parser.add_argument("--opponents", type=int, default=2, help="real emily-bot opponent processes to launch")
    # --fast-forward defaults ON now, matching BRAWLPIT's own rl_train_packet.py exactly (that
    # script hardcodes it unconditionally, no flag at all -- see _spawn_server's own doc comment).
    # Real, found-live correction of this file's own earlier, more conservative default-OFF
    # choice: the theoretical concern (server races ahead of a single-threaded Python learner's
    # step() rate, making the real tick count between one prev/cur reward pair large and
    # uncontrolled, adding noise to the small per-tick shaping terms) is real, but BRAWLPIT's own
    # live registry proves it doesn't actually block real learning in practice -- real checkpoints
    # at generation 549 with real, sane Elo values, trained the exact same way. Kept as a real,
    # overridable flag (--no-fast-forward) rather than removed entirely, for a debugging session
    # that specifically wants real-time tick correspondence.
    parser.add_argument("--fast-forward", action="store_true", default=True)
    parser.add_argument("--no-fast-forward", dest="fast_forward", action="store_false")
    parser.add_argument("--own-server", action="store_true", default=True,
                        help="launch a fresh, isolated shank_server for this training run (default) rather than attach to an already-running one")
    parser.add_argument("--attach", action="store_true", help="attach to an already-running server at --host/--port instead of launching one")
    parser.add_argument("--out", default="var/rl_checkpoints/ppo_shankpit_queue.zip")
    args = parser.parse_args()

    if gym is None:
        print("gymnasium not installed -- cannot train")
        return 1
    try:
        from stable_baselines3 import PPO
    except ImportError:
        print("stable_baselines3 not installed -- cannot train")
        return 1

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    server_bin = os.path.join(repo_root, "bin", "shank_server")
    bot_bin = os.path.join(repo_root, "bin", "emily-bot")

    server_proc = None
    bot_procs = []
    if not args.attach:
        print(f"[train] launching isolated shank_server on port {args.port} (fast_forward={args.fast_forward})")
        server_proc = launch_server(server_bin, args.port, args.fast_forward)
        time.sleep(1.0)
        if args.opponents > 0:
            print(f"[train] launching {args.opponents} real emily-bot opponent(s)")
            bot_procs = launch_opponent_bots(bot_bin, args.host, args.port, args.opponents, "24h")
            time.sleep(1.0)

    try:
        env = ShankpitQueueEnv(host=args.host, port=args.port, max_episode_steps=args.max_episode_steps)
        print("[train] connecting real PPO learner to the env...")
        model = PPO("MlpPolicy", env, verbose=1, n_steps=512, batch_size=64)
        print(f"[train] starting real training run: {args.timesteps} timesteps")
        model.learn(total_timesteps=args.timesteps)

        out_path = os.path.join(repo_root, args.out)
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        model.save(out_path)
        print(f"[train] real checkpoint saved: {out_path}")
        env.close()
        return 0
    finally:
        for p in bot_procs:
            p.terminate()
        if server_proc is not None:
            server_proc.terminate()


if __name__ == "__main__":
    sys.exit(main())
