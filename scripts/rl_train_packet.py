#!/usr/bin/env python3
"""
scripts/rl_train_packet.py (S459-54) -- the real training orchestrator: runs THREE simultaneous
PPO models (Main, Main Exploiter, League Exploiter -- scripts/rl_league.py's own real AlphaStar
league roles, already ported from BRAWLPIT/REDGARDEN, S459-35) against SHANKPIT's real
packet-level env (scripts/rl_env_packet.py), and, on every checkpoint-save cycle, registers all
three as one real "snapshot" into the shared league (register_generation_snapshot) -- matching
BRAWLPIT's own real design. Founder real-time, after being told the earlier single-policy
version wasn't training a real league at all: "i said just like brawlpit" -> "build it."

Real, necessary architecture difference from BRAWLPIT, checked and accepted directly ("if it
needs to be 1v1 thats fine"): BRAWLPIT's own packet env drives BOTH sides of one match from a
single Python process (one observation carries own_state + opp_state), so swapping in a frozen
self-play opponent is an in-process model swap. SHANKPIT's server is a real, continuous, live
world where each UDP connection is exactly one independent player (S459-44/45's own real find) --
there is no "opponent slot" inside one process to swap a model into. The real, correct analog
here is scripts/frozen_policy_bot.py (S459-54, new): a SEPARATE OS process that connects as its
own real player and runs a frozen checkpoint's own policy.predict() loop every tick, the same way
apps2/emily-bot runs a heuristic loop -- just with a PPO forward pass instead of hand-written
rules. This orchestrator spawns one of these per self-play opponent.

Real, deliberate scope-down from BRAWLPIT's own 981-line orchestrator, named honestly rather than
silently dropped:
  - No --num-envs parallel rollout collection (SubprocVecEnv) -- one env per role per generation,
    matching this pipeline's own existing S459-48 design. Real future work if training speed
    becomes the bottleneck once this actually runs for a while.
  - No native-inference weight export (scripts/export_policy_weights.py has no SHANKPIT
    equivalent yet -- matches S459-49's own already-documented scope-down in checkpoint_store.go).
  - Evaluation is a real, simple kill-count comparison over a fixed real-time window (frozen_
    policy_bot.py's own --report-kills-to), not BRAWLPIT's own dedicated rl_evaluate.py harness
    (SHANKPIT has no PACKET_RESET_MATCH / real match-boundary concept to build a cleaner one on
    top of -- see rl_env_packet.py's own module doc comment for why QUEUE's continuous-respawn
    design makes "one life = one episode" the natural unit instead).

What IS a faithful, real port: PFSP-weighted opponent selection (rl_league.py's own sample_for_*
functions, unchanged), register_generation_snapshot (all 3 archetypes together per generation),
Main Exploiter's periodic reset, Main's own regression guard (protects against PPO catastrophic
forgetting in self-play), --resume-from-registry, --registry-url push.
"""

import argparse
import atexit
import json
import os
import signal
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from rl_league import (  # noqa: E402
    DEFAULT_ELO,
    HEURISTIC_ID,
    LeagueManager,
    LeagueRole,
    register_generation_snapshot,
    sample_for_league_exploiter,
    sample_for_main,
    sample_for_main_exploiter,
    should_reset_main_exploiter,
)

try:
    from stable_baselines3 import PPO
    from stable_baselines3.common.callbacks import BaseCallback
    _HAVE_SB3 = True
except ImportError:
    _HAVE_SB3 = False
    BaseCallback = object

from rl_env_packet import ShankpitQueueEnv, gym as _gym  # noqa: E402
from rl_registry import authenticate, download_checkpoint, list_checkpoints, push_checkpoint  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVER_BIN = os.path.join(REPO_ROOT, "bin", "shank_server")
BOT_BIN = os.path.join(REPO_ROOT, "bin", "emily-bot")
FROZEN_BOT_SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "frozen_policy_bot.py")

# One dedicated server port per archetype -- matches BRAWLPIT's own ROLE_BASE_PORTS shape (a real
# 100-port-wide reserved block per role, headroom for future --num-envs-style parallelism even
# though this pass doesn't use it).
ROLE_BASE_PORTS = {
    LeagueRole.MAIN: 17978,
    LeagueRole.MAIN_EXPLOITER: 18078,
    LeagueRole.LEAGUE_EXPLOITER: 18178,
}
EVAL_PORT = 18278  # past every role's own reserved block, so it can never collide

EVAL_DURATION_SECONDS = 30.0  # real wall-clock window for a per-generation evaluation match -- short by design (this runs up to 3x every generation, see BRAWLPIT's own EVAL_MAX_TICKS doc comment for the identical real performance rationale), matching this file's own EVAL_PORT server running --fast-forward
# EVAL_STARTUP_GRACE_SECONDS (S459-64) -- real, generous headroom on TOP of EVAL_DURATION_SECONDS
# for real subprocess startup cost (PPO.load() + torch/sb3 import overhead, incurred by BOTH eval
# bots concurrently) BEFORE the match's own 30s window even starts ticking -- a real, found-live
# Colab bug: the old +15s margin was sized for a fast, uncontended dev CPU and every real Colab
# evaluation match timed out against it, permanently masking real Elo movement as "tied."
EVAL_STARTUP_GRACE_SECONDS = 90.0

REGRESSION_ELO_THRESHOLD = 100.0  # same real, deliberately conservative constant BRAWLPIT's own regression guard uses -- see _should_revert_main's own doc comment

# S459-51's own real entropy-collapse fix, ported here too -- SB3's PPO defaults ent_coef=0.0,
# providing zero pressure against the Gaussian action distribution's log_std collapsing toward
# zero variance (see BRAWLPIT/scripts/rl_train_packet.py's own DEFAULT_ENT_COEF doc comment for
# the full mechanism). Applied here from the start rather than found the hard way a second time.
DEFAULT_ENT_COEF = 0.01

_spawned_procs = []


def _spawn_server(port):
    """Starts one real bin/shank_server --deathmatch --fast-forward --port <port> subprocess."""
    proc = subprocess.Popen([SERVER_BIN, "--deathmatch", "--fast-forward", "--port", str(port)],
                             cwd=REPO_ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    _spawned_procs.append(proc)
    time.sleep(0.5)  # real, minimal startup grace period -- server_net_init binds synchronously
    return proc


def _spawn_heuristic_bots(host, port, count):
    procs = []
    for _ in range(count):
        proc = subprocess.Popen(
            [BOT_BIN, "-host", host, "-port", str(port), "-mode", "108", "-no-report",
             "-session-duration", "24h"],
            cwd=REPO_ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        _spawned_procs.append(proc)
    return procs


def _spawn_frozen_policy_bot(host, port, checkpoint_path, session_duration=None, report_kills_to=None, log_path=None):
    """Launches scripts/frozen_policy_bot.py as a real, separate OS process -- the real self-play
    primitive this file's own module doc comment names (SHANKPIT has no in-process opponent slot
    the way BRAWLPIT's own packet env does). log_path (S459-61), when given, captures real
    stdout+stderr to a file instead of silencing it -- a crashed bot's own traceback used to be
    invisible (subprocess.DEVNULL), indistinguishable from a real, successful 0-0 tie. None
    (every non-evaluation caller -- the real self-play/heuristic opponents spawned during actual
    training) keeps the original DEVNULL behavior; those run for the whole generation and would
    otherwise flood the parent's own log."""
    cmd = [sys.executable, FROZEN_BOT_SCRIPT, "--host", host, "--port", str(port),
           "--checkpoint", checkpoint_path]
    if session_duration is not None:
        cmd += ["--session-duration", str(session_duration)]
    if report_kills_to is not None:
        cmd += ["--report-kills-to", report_kills_to]
    if log_path is not None:
        log_f = open(log_path, "w")
        proc = subprocess.Popen(cmd, cwd=REPO_ROOT, stdout=log_f, stderr=subprocess.STDOUT)
        proc._shankpit_log_file = log_f  # closed by the caller once the process has exited
    else:
        proc = subprocess.Popen(cmd, cwd=REPO_ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    _spawned_procs.append(proc)
    return proc


def _cleanup_procs():
    for proc in _spawned_procs:
        proc.terminate()
    for proc in _spawned_procs:
        try:
            proc.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            proc.kill()


atexit.register(_cleanup_procs)


def _handle_terminate_signal(signum, frame):
    """Real, found-live gap BRAWLPIT's own rl_train_packet.py already named and fixed (ported
    here too): Python's atexit hooks do NOT run on a bare SIGTERM, only on normal interpreter
    exit/sys.exit()/an uncaught exception -- without this, killing a real training run via
    `pkill`/most process managers leaves every spawned server/bot subprocess running forever."""
    sys.exit(0)


signal.signal(signal.SIGTERM, _handle_terminate_signal)


def _pick_opponent_checkpoint(role, league, local_wins_losses, recent_results_vs_main, rng=None):
    """Thin adapter onto rl_league.py's own real sample_for_* functions -- a direct, faithful
    port of BRAWLPIT's own identically-named function (see that file's own doc comment for the
    exact per-role PFSP math this calls into unchanged). Returns (checkpoint_path, member_id),
    both None when nothing real exists yet to sample (generation 0's own bootstrap case) or the
    sample landed on the permanent HEURISTIC_ID baseline (no real checkpoint file backs it)."""
    id_to_path = {m.id: m.path for m in league.all_members()}
    if role == LeagueRole.MAIN:
        picked_id = sample_for_main(league, local_wins_losses, rng=rng)
    elif role == LeagueRole.MAIN_EXPLOITER:
        picked_id = sample_for_main_exploiter(league, local_wins_losses, recent_results_vs_main, rng=rng)
    else:
        picked_id = sample_for_league_exploiter(league, local_wins_losses, rng=rng)
    if picked_id is None or picked_id == HEURISTIC_ID or picked_id not in id_to_path:
        return None, None
    return id_to_path[picked_id], picked_id


def _should_revert_main(new_elo, best_elo_so_far, threshold=REGRESSION_ELO_THRESHOLD):
    """A direct, faithful port of BRAWLPIT's own _should_revert_main -- see
    REGRESSION_ELO_THRESHOLD's own doc comment for the real rationale (PPO catastrophic
    forgetting in a self-play setting)."""
    return best_elo_so_far - new_elo >= threshold


def _run_evaluation_match(host, port, checkpoint_a, checkpoint_b, duration_seconds=EVAL_DURATION_SECONDS):
    """Real, minimal 1v1 evaluation match -- SHANKPIT's own necessary analog to BRAWLPIT's
    dedicated rl_evaluate.py (no PACKET_RESET_MATCH / match-boundary concept exists here to build
    a cleaner one on top of, see this module's own top-of-file doc comment). Spawns two real
    frozen_policy_bot.py processes on an isolated, --fast-forward server, lets them fight for a
    real, fixed wall-clock window, and compares final kill counts (each bot reports its own via
    --report-kills-to) -- a real, honest, coarse-but-fast proxy for "who's better," the same
    real tradeoff BRAWLPIT's own EVAL_MAX_TICKS cap accepts for the identical reason (this runs
    up to 3x every single generation, so it has to stay fast, not full-match-length).

    Returns (score_a, note): score_a is 1.0 (A won on kills), 0.0 (B won), 0.5 (tied or either
    report failed). note (S459-63) is a real, short, human-readable summary of what actually
    happened -- real kill counts on a genuine result, or the captured crash/report-failure reason
    -- meant to be pushed alongside the checkpoint itself (rl_registry.py's own push_checkpoint
    eval_note parameter) so it's visible through the registry API without needing access to this
    process's own stdout (founder real-time: "how the fuck is my colab log gonna help it just
    says training")."""
    server = _spawn_server(port)
    try:
        with tempfile.TemporaryDirectory() as tmpdir:
            report_a = os.path.join(tmpdir, "a.json")
            report_b = os.path.join(tmpdir, "b.json")
            # S459-61: these two bots used to run with stdout/stderr silenced entirely
            # (subprocess.DEVNULL) -- a real crash (bad checkpoint load, a Colab-specific
            # environment gap, etc.) was invisible, indistinguishable in the log from a genuine
            # 0-0 tie. Captured to real files instead so a crash's own traceback is printed below
            # when the report can't be read, matching this pipeline's own established S439
            # Colab-output-visibility precedent.
            log_a = os.path.join(tmpdir, "a.log")
            log_b = os.path.join(tmpdir, "b.log")
            bot_a = _spawn_frozen_policy_bot(host, port, checkpoint_a, duration_seconds, report_a, log_a)
            bot_b = _spawn_frozen_policy_bot(host, port, checkpoint_b, duration_seconds, report_b, log_b)
            # S459-64, real, found-live bug via the new S459-63 eval_note diagnostic itself:
            # founder's own real Colab run showed every single evaluation match failing with
            # "timed out after 45.0 seconds" (duration_seconds=30 + the old +15 grace) -- on
            # every generation, both roles, never once succeeding. The prior +15s margin was
            # sized for this dev box's own fast, uncontended CPU; it never accounted for real
            # startup cost BEFORE the 30s match window even starts ticking (PPO.load() +
            # torch/sb3 import overhead, TWICE, running CONCURRENTLY for bot_a and bot_b, on a
            # slower/shared Colab CPU) -- that startup cost alone can plausibly exceed 15s under
            # real contention, pushing the total past the old deadline even though the match
            # itself would have finished normally. EVAL_STARTUP_GRACE_SECONDS is a real, generous
            # bump (not a guess at exactly how slow Colab is -- a deliberately wide margin, since
            # this only matters in the slow-startup case, not the common one) so a genuinely slow
            # environment gets real headroom instead of every eval silently degrading to "tied."
            bot_a.wait(timeout=duration_seconds + EVAL_STARTUP_GRACE_SECONDS)
            bot_b.wait(timeout=duration_seconds + EVAL_STARTUP_GRACE_SECONDS)
            for p in (bot_a, bot_b):
                if p in _spawned_procs:
                    _spawned_procs.remove(p)
                # Flush+close the captured log file now that the process has exited, so _tail()
                # below reads everything the bot actually wrote, not a partially-buffered file.
                log_f = getattr(p, "_shankpit_log_file", None)
                if log_f is not None:
                    log_f.close()

            # S459-61, real, found-live gap: this function used to return a silent 0.5 for BOTH
            # a genuine 0-0 tie (both bots really did fight for the full window and neither
            # landed a kill -- a real, plausible outcome this early in training, especially at
            # EVAL_DURATION_SECONDS=30s) and a bot crash/report-write failure -- founder
            # real-time: "i have 2 gens same elo seems wrong" gave no way to tell which was
            # actually happening from the training log alone. Now prints which case it was, so
            # the NEXT run's own log answers the question directly instead of needing a guess.
            def _tail(path, n=15):
                try:
                    with open(path) as f:
                        lines = f.readlines()
                    return "".join(lines[-n:]).rstrip() or "(empty)"
                except OSError:
                    return "(log file missing)"

            try:
                with open(report_a) as f:
                    kills_a = json.load(f)["kills"]
            except (FileNotFoundError, json.JSONDecodeError, KeyError) as e:
                tail_a = _tail(log_a, n=3)  # short -- this same text also has to fit in the pushed eval_note
                print(f"    [eval] checkpoint A ({os.path.basename(checkpoint_a)}) report unreadable ({e}) "
                      f"-- treating as a draw. bot A's own output:\n{_tail(log_a)}", flush=True)
                return 0.5, f"CRASH: bot A report unreadable ({e}); log tail: {tail_a}"
            try:
                with open(report_b) as f:
                    kills_b = json.load(f)["kills"]
            except (FileNotFoundError, json.JSONDecodeError, KeyError) as e:
                tail_b = _tail(log_b, n=3)
                print(f"    [eval] checkpoint B ({os.path.basename(checkpoint_b)}) report unreadable ({e}) "
                      f"-- treating as a draw. bot B's own output:\n{_tail(log_b)}", flush=True)
                return 0.5, f"CRASH: bot B report unreadable ({e}); log tail: {tail_b}"

            print(f"    [eval] {os.path.basename(checkpoint_a)} kills={kills_a} vs "
                  f"{os.path.basename(checkpoint_b)} kills={kills_b} (both reports read fine)", flush=True)
            note = f"vs prior gen: kills {kills_a}-{kills_b}"
            if kills_a > kills_b:
                return 1.0, note
            if kills_a < kills_b:
                return 0.0, note
            return 0.5, note  # a real, genuine tie -- both bots reported successfully with equal kills
    finally:
        server.terminate()
        try:
            server.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            server.kill()
        if server in _spawned_procs:
            _spawned_procs.remove(server)


def _fresh_model(env, device, ent_coef=DEFAULT_ENT_COEF):
    return PPO("MlpPolicy", env, verbose=0, device=device, ent_coef=ent_coef, n_steps=512, batch_size=64)


def _load_resumed_model_or_fresh(checkpoint_path, env, device, role_value, generation, ent_coef=DEFAULT_ENT_COEF):
    """Real, found-relevant guard ported from BRAWLPIT: PPO.load raises ValueError on any
    observation/action-space mismatch (e.g. a future OBS_SIZE change) -- falls back to a fresh
    model for THIS role only rather than crashing the whole run."""
    try:
        model = PPO.load(checkpoint_path, env=env, device=device)
        print(f"[gen {generation}] {role_value}: resumed from registry checkpoint")
        return model
    except ValueError as e:
        print(f"[gen {generation}] {role_value}: WARNING -- registry checkpoint {checkpoint_path} "
              f"is incompatible with the current observation/action space ({e}); starting this "
              f"role COMPLETELY FRESH instead of crashing.")
        return _fresh_model(env, device, ent_coef=ent_coef)


class _HeartbeatCallback(BaseCallback):
    """Real, found-necessary progress signal, ported from BRAWLPIT (S437's own rationale applies
    identically here): model.learn() with verbose=0 produces zero output until a whole chunk
    finishes, indistinguishable from "frozen" on a slow/CPU-starved box."""

    HEARTBEAT_STEPS = 200

    def __init__(self, role_name):
        super().__init__()
        self.role_name = role_name
        self._start_time = None
        self._start_timesteps = None

    def _on_training_start(self):
        self._start_time = time.time()
        self._start_timesteps = self.num_timesteps

    def _on_step(self):
        done_this_chunk = self.num_timesteps - self._start_timesteps
        if done_this_chunk > 0 and done_this_chunk % self.HEARTBEAT_STEPS == 0:
            elapsed = time.time() - self._start_time
            fps = done_this_chunk / elapsed if elapsed > 0 else 0.0
            print(f"  [heartbeat] {self.role_name}: {done_this_chunk} steps this chunk, "
                  f"{elapsed:.0f}s elapsed, {fps:.1f} steps/sec", flush=True)
        return True


def _find_latest_registry_checkpoint(registry_url, role_value):
    """Real, live lookup for --resume-from-registry -- skips any checkpoint marked is_disabled
    (S459-49's own real NOCK checkbox, enforced here the same way BRAWLPIT's own identical
    function already does)."""
    checkpoints = [c for c in list_checkpoints(registry_url, role=role_value) if not c.get("is_disabled")]
    if not checkpoints:
        return None
    return max(checkpoints, key=lambda c: (c["generation"], c["id"]))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--total-timesteps", type=int, default=200_000)
    p.add_argument("--save-freq", type=int, default=4096)
    p.add_argument("--league-dir", default=os.environ.get("SHANKPIT_LEAGUE_DIR", "league_data"))
    p.add_argument("--reset-every-n-generations", type=int, default=5,
                   help="Main Exploiter's own periodic full reset cadence. <= 0 disables it.")
    p.add_argument("--output-dir", default=os.environ.get("SHANKPIT_RL_OUTPUT_DIR", "rl_packet_checkpoints"))
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--heuristic-opponents", type=int, default=1,
                   help="real emily-bot heuristic opponents to launch alongside every role's own "
                        "training env, in addition to (or instead of, at generation 0) a real "
                        "self-play opponent.")
    p.add_argument("--registry-url", default=os.environ.get("IDUNA_BASE_URL"),
                   help="e.g. https://okemily.com -- if set, every generation's 3 checkpoints "
                        "also push to IDUNA's real, shared checkpoint registry.")
    p.add_argument("--registry-agent-name", default=os.environ.get("IDUNA_AGENT_NAME", "SHANKPIT-RL"))
    p.add_argument("--registry-agent-secret", default=os.environ.get("IDUNA_AGENT_SECRET"))
    p.add_argument("--registry-source-location", default=os.environ.get("SHANKPIT_SOURCE_LOCATION", "unknown"))
    p.add_argument("--resume-from-registry", action="store_true",
                   help="requires --registry-url. Warm-starts each role from the newest checkpoint "
                        "that role already has in the shared registry instead of a fresh network.")
    p.add_argument("--device", default=os.environ.get("SHANKPIT_RL_DEVICE", "cpu"),
                   help="stable_baselines3 device. Defaults to 'cpu' -- this pipeline's own tiny "
                        "MLP policy plus one real UDP round trip per environment step is "
                        "latency-bound, not compute-bound (same real measured finding BRAWLPIT's "
                        "own rl_train_packet.py already documents).")
    p.add_argument("--ent-coef", type=float, default=DEFAULT_ENT_COEF)
    p.add_argument("--max-episode-steps", type=int, default=1000)
    args = p.parse_args()

    registry_jwt = None
    if args.registry_url:
        if not args.registry_agent_secret:
            print("--registry-url was set but --registry-agent-secret (or IDUNA_AGENT_SECRET) "
                  "wasn't -- refusing to silently skip the registry push.")
            return 1
        registry_jwt = authenticate(args.registry_url, args.registry_agent_name, args.registry_agent_secret)
        print(f"Authenticated with the remote checkpoint registry at {args.registry_url}.")

    if args.resume_from_registry and not args.registry_url:
        print("--resume-from-registry needs --registry-url (or IDUNA_BASE_URL) set.")
        return 1

    if not _HAVE_SB3 or _gym is None:
        print("stable_baselines3 and/or gymnasium are not installed -- nothing was run.")
        return 1
    if not os.path.exists(SERVER_BIN):
        print(f"{SERVER_BIN} not found -- run `make server` first.")
        return 1
    if not os.path.exists(BOT_BIN):
        print(f"{BOT_BIN} not found -- run `make emily-bot` first.")
        return 1

    os.makedirs(args.output_dir, exist_ok=True)
    league = LeagueManager(args.league_dir)

    prev_checkpoint_paths, prev_member_ids, prev_remote_ids = {}, {}, {}
    resume_generation = -1

    if args.resume_from_registry:
        for role in ROLE_BASE_PORTS:
            latest = _find_latest_registry_checkpoint(args.registry_url, role.value)
            if latest is None:
                print(f"resume: no existing registry checkpoint for {role.value} yet -- starts fresh.")
                continue
            local_path = os.path.join(args.output_dir, f"_resume_{role.value}.zip")
            download_checkpoint(args.registry_url, latest["id"], local_path)
            prev_checkpoint_paths[role] = local_path
            prev_remote_ids[role] = latest["id"]
            seeded = league.register(role.value, latest["generation"], local_path, inherit_elo_from_role=False)
            league.set_elo(seeded.id, latest["elo"])
            prev_member_ids[role] = seeded.id
            resume_generation = max(resume_generation, latest["generation"])
            print(f"resume: {role.value} <- registry checkpoint id={latest['id']} "
                  f"(gen {latest['generation']}, elo={latest['elo']:.0f})")

    models = {}
    checkpoint_template = os.path.join(args.output_dir, "{role}_gen{gen}")
    timesteps_done = {role: 0 for role in ROLE_BASE_PORTS}
    generation = resume_generation + 1

    local_wins_losses = {role: {} for role in ROLE_BASE_PORTS}
    recent_results_vs_main = []

    best_elo = {LeagueRole.MAIN: DEFAULT_ELO}
    best_checkpoint_path = {}
    best_member_id = {}
    if LeagueRole.MAIN in prev_member_ids:
        best_elo[LeagueRole.MAIN] = league.get_elo(prev_member_ids[LeagueRole.MAIN])
        best_checkpoint_path[LeagueRole.MAIN] = prev_checkpoint_paths[LeagueRole.MAIN]
        best_member_id[LeagueRole.MAIN] = prev_member_ids[LeagueRole.MAIN]

    while min(timesteps_done.values()) < args.total_timesteps:
        checkpoint_paths = {}
        reset_roles = set()
        self_play_opponent_ids = {}

        for role in ROLE_BASE_PORTS:
            chunk = min(args.save_freq, args.total_timesteps - timesteps_done[role])
            if chunk <= 0:
                checkpoint_paths[role] = checkpoint_template.format(role=role.value, gen=generation) + ".zip"
                continue

            port = ROLE_BASE_PORTS[role]
            _spawn_server(port)

            self_play_opponent, self_play_opponent_id = _pick_opponent_checkpoint(
                role, league, local_wins_losses[role], recent_results_vs_main)
            self_play_opponent_ids[role] = self_play_opponent_id
            if self_play_opponent:
                _spawn_frozen_policy_bot(args.host, port, self_play_opponent)
                print(f"[gen {generation}] {role.value}: real self-play opponent ({self_play_opponent_id})", flush=True)
            else:
                print(f"[gen {generation}] {role.value}: no league members registered yet -- heuristic-only bootstrap", flush=True)
            if args.heuristic_opponents > 0:
                _spawn_heuristic_bots(args.host, port, args.heuristic_opponents)
            time.sleep(1.0)  # real grace period for opponents to connect+spawn before the trainee starts stepping

            env = ShankpitQueueEnv(host=args.host, port=port, max_episode_steps=args.max_episode_steps)

            if role not in models:
                if role in prev_checkpoint_paths:
                    models[role] = _load_resumed_model_or_fresh(
                        prev_checkpoint_paths[role], env, args.device, role.value, generation,
                        ent_coef=args.ent_coef)
                else:
                    models[role] = _fresh_model(env, args.device, ent_coef=args.ent_coef)
                    print(f"[gen {generation}] {role.value}: fresh PPO model")
            else:
                models[role].set_env(env)
            model = models[role]

            print(f"[gen {generation}] {role.value}: training {chunk} timesteps...", flush=True)
            before = model.num_timesteps
            model.learn(total_timesteps=chunk, reset_num_timesteps=False, callback=_HeartbeatCallback(role.value))
            timesteps_done[role] += model.num_timesteps - before

            ckpt_path = checkpoint_template.format(role=role.value, gen=generation)
            model.save(ckpt_path)
            checkpoint_paths[role] = ckpt_path + ".zip"
            print(f"[gen {generation}] {role.value}: saved {ckpt_path}.zip "
                  f"({timesteps_done[role]}/{args.total_timesteps} timesteps)")

            if role == LeagueRole.MAIN_EXPLOITER and should_reset_main_exploiter(generation, args.reset_every_n_generations):
                print(f"[gen {generation}] Main Exploiter: resetting to a freshly initialized network.")
                models[role] = _fresh_model(env, args.device, ent_coef=args.ent_coef)
                reset_roles.add(role)

            env.close()
            # Real, deliberate per-role teardown -- only the actively-training role's own
            # server/opponents are ever running at a time, matching BRAWLPIT's own S441 real
            # CPU-saving redesign (idle roles spawn nothing at all).
            still_running = [pr for pr in _spawned_procs]
            for proc in still_running:
                proc.terminate()
            for proc in still_running:
                try:
                    proc.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    proc.kill()
                if proc in _spawned_procs:
                    _spawned_procs.remove(proc)

        registered = register_generation_snapshot(league, generation, checkpoint_paths, reset_roles=reset_roles)
        for role, member in registered.items():
            print(f"[gen {generation}] registered {role.value} -> league member {member.id} "
                  f"(elo={league.get_elo(member.id):.0f}, inherited -- not yet evaluated this generation)")

        # S459-60, real, found-live bug (same category as BRAWLPIT's own S424 "ELOs stuck at
        # 1500" -- the piece that actually MOVES Elo wasn't connected to what gets reported):
        # this evaluation-match block, which is the ONLY thing that ever calls
        # league.record_match_result and moves a checkpoint's real Elo, used to run AFTER the
        # push-to-registry block below. So every checkpoint was pushed to IDUNA carrying its
        # pre-evaluation, purely-INHERITED Elo (1500 forever at generation 0, and never updated
        # again after that, since push_checkpoint is a one-shot POST -- IDUNA has no endpoint to
        # patch a checkpoint's Elo after the fact). The local league's own Elo WAS moving
        # correctly the whole time; it just never reached the remote registry NOCK actually
        # displays. Fixed by running evaluation first, so the Elo pushed below is the real,
        # current, post-match number.
        main_reverted_to = None
        eval_notes = {}  # S459-63: role -> real, short summary of THIS generation's own vs-prior-gen eval, pushed alongside the checkpoint
        for role, member in registered.items():
            if role in reset_roles or role not in prev_checkpoint_paths:
                eval_notes[role] = "no prior generation to evaluate against yet"
                continue
            try:
                score_a, note = _run_evaluation_match(args.host, EVAL_PORT, checkpoint_paths[role], prev_checkpoint_paths[role])
                eval_notes[role] = note
                league.record_match_result(member.id, prev_member_ids[role], score_a)
                new_elo, prev_elo = league.get_elo(member.id), league.get_elo(prev_member_ids[role])
                print(f"[gen {generation}]   -> evaluated {role.value} vs its own prior generation: "
                      f"score_a={score_a} (local elo now {new_elo:.0f} vs {prev_elo:.0f})")

                if role == LeagueRole.MAIN:
                    if new_elo >= best_elo[LeagueRole.MAIN]:
                        best_elo[LeagueRole.MAIN] = new_elo
                        best_checkpoint_path[LeagueRole.MAIN] = checkpoint_paths[role]
                        best_member_id[LeagueRole.MAIN] = member.id
                    elif LeagueRole.MAIN in best_checkpoint_path and _should_revert_main(new_elo, best_elo[LeagueRole.MAIN]):
                        print(f"[gen {generation}]   -> REGRESSION GUARD: main's new elo ({new_elo:.0f}) "
                              f"dropped {best_elo[LeagueRole.MAIN] - new_elo:.0f} below its own best-ever "
                              f"({best_elo[LeagueRole.MAIN]:.0f}) -- reverting the LIVE model, the "
                              f"regressed checkpoint stays in the registry permanently either way.")
                        models[LeagueRole.MAIN] = PPO.load(best_checkpoint_path[LeagueRole.MAIN], device=args.device)
                        main_reverted_to = (best_checkpoint_path[LeagueRole.MAIN], best_member_id[LeagueRole.MAIN])
            except Exception as e:  # noqa: BLE001 -- an evaluation match failing must never crash real, in-progress training
                eval_notes[role] = f"CRASH: evaluation match itself failed ({e})"
                print(f"[gen {generation}]   -> WARNING: evaluation match for {role.value} failed ({e}), Elo unchanged")

        remote_ids = {}
        for role, member in registered.items():
            elo = league.get_elo(member.id)  # the real, current, post-evaluation Elo
            if registry_jwt:
                try:
                    remote = push_checkpoint(args.registry_url, registry_jwt, role.value, generation,
                                              elo, args.registry_source_location, checkpoint_paths[role],
                                              eval_note=eval_notes.get(role, ""))
                    remote_ids[role] = remote["id"]
                    print(f"[gen {generation}]   -> pushed to remote registry as checkpoint id={remote['id']} (elo={elo:.0f})")
                except Exception as e:  # noqa: BLE001 -- a registry outage must never crash a real, in-progress training run
                    print(f"[gen {generation}]   -> WARNING: push to remote registry failed ({e}), continuing locally")

        for role, member in registered.items():
            opponent_id = self_play_opponent_ids.get(role)
            if opponent_id is None or role in reset_roles:
                continue
            id_to_path = {m.id: m.path for m in league.all_members()}
            opponent_path = id_to_path.get(opponent_id)
            if opponent_path is None:
                continue
            try:
                score_a, _note = _run_evaluation_match(args.host, EVAL_PORT, checkpoint_paths[role], opponent_path)
                wins, losses = local_wins_losses[role].get(opponent_id, (0, 0))
                if score_a == 1.0:
                    local_wins_losses[role][opponent_id] = (wins + 1, losses)
                elif score_a == 0.0:
                    local_wins_losses[role][opponent_id] = (wins, losses + 1)
                current_main = league.latest_by_role(LeagueRole.MAIN)
                if role == LeagueRole.MAIN_EXPLOITER and current_main is not None and opponent_id == current_main.id and score_a != 0.5:
                    recent_results_vs_main.append(1 if score_a == 1.0 else 0)
                    recent_results_vs_main[:] = recent_results_vs_main[-20:]
                print(f"[gen {generation}]   -> PFSP feedback: {role.value} vs {opponent_id}: score_a={score_a}")
            except Exception as e:  # noqa: BLE001
                print(f"[gen {generation}]   -> WARNING: PFSP feedback match for {role.value} failed ({e})")

        prev_checkpoint_paths = dict(checkpoint_paths)
        prev_member_ids = {role: member.id for role, member in registered.items()}
        prev_remote_ids = remote_ids
        if main_reverted_to is not None:
            prev_checkpoint_paths[LeagueRole.MAIN], prev_member_ids[LeagueRole.MAIN] = main_reverted_to

        generation += 1

    print("DONE.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
