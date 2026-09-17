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
from rl_registry import authenticate, download_checkpoint, fetch_default_queue_level, list_checkpoints, push_checkpoint, push_heartbeat, update_checkpoint_elo  # noqa: E402

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

EVAL_DURATION_SECONDS = 30.0  # real MAX wall-clock safety cap for a per-generation evaluation match, not the primary stopping condition (see EVAL_KILLS_TO below) -- short by design (this runs up to 3x every generation, see BRAWLPIT's own EVAL_MAX_TICKS doc comment for the identical real performance rationale), matching this file's own EVAL_PORT server running --fast-forward
# EVAL_KILLS_TO (S474 follow-up, founder real-time: "first to 1 may not be as good as like first
# to 5" / "we didnt turn brawlpit down to stock 1" / "first to 5 is roughly equivalent to 5
# stock") -- the real primary stopping condition for an evaluation match as of this pass: whoever
# reaches this many kills first wins, matching BRAWLPIT's own real 5-stock convention instead of a
# single decisive kill deciding the whole match. A single-kill outcome is genuinely noisy this
# early in training (confirmed directly against the real, live registry: score_a flipping
# 1.0/0.0/1.0 between ADJACENT generations, real kill counts like "3-7"/"9-13" from actual
# matches) -- first positioning luck or one early engagement shouldn't be the whole signal.
# EVAL_DURATION_SECONDS above stays as a real, honest safety cap for the case neither side ever
# reaches this many kills (two very passive/weak policies) -- see _run_evaluation_match's own
# doc comment for exactly how the two combine.
EVAL_KILLS_TO = 5
EVAL_POLL_INTERVAL_SECONDS = 1.0  # matches frozen_policy_bot.py's own --report-interval default order of magnitude
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

# S459-104: authenticate() issues a real, short-lived (1hr) Bearer JWT -- refresh proactively at
# 45 minutes so a long training run never has a real gap where it's silently running on an
# expired token (see the refresh call site's own doc comment for the full "we train a bunch that
# never gets checked in" bug this closes).
REGISTRY_JWT_REFRESH_SECONDS = 45 * 60

_spawned_procs = []

# S459-104, founder real-time: "DEFAULT FOR QUEUE SHOULD SET TRAINING LEVEL" -- set once at
# startup by main() (fetch_default_queue_level below), read by every _spawn_server call. None
# means no level is currently flagged is_default_queue (or the registry was unreachable) -- the
# real, honest fallback is the built-in --deathmatch scene rotation, same as before this existed.
_training_level_path = None

# _server_log_dir (S480 follow-up, founder real-time report: a real Colab run's very first
# env.reset() raised "TimeoutError: no live respawn snapshot within timeout" with ZERO other
# diagnostic output -- the exact same class of bug S459-61 already found and fixed for the two
# eval bots ("these two bots used to run with stdout/stderr silenced entirely... a real crash...
# was invisible, indistinguishable in the log from a genuine 0-0 tie"), left unfixed here for the
# training server itself. Set once at startup by main() to args.output_dir; None (an older/direct
# caller, e.g. this module's own CLI before main() runs) falls back to a real temp dir rather than
# erroring.
_server_log_dir = None


def _spawn_server(port):
    """Starts one real bin/shank_server --deathmatch --fast-forward --port <port> subprocess --
    plus --level <path> when a level is currently flagged is_default_queue in NOCK's SHANKPIT
    level editor (see fetch_default_queue_level's own doc comment for the real gap this closes).

    Real, found-live fix: stdout/stderr used to go to subprocess.DEVNULL unconditionally -- if the
    server binary crashed or failed to start at all (a stale/broken build, a missing shared
    library, a Colab-specific environment gap), NOTHING about why was ever visible; the caller
    just eventually got a downstream TimeoutError from _wait_for_alive_snapshot 30 real seconds
    later with zero context, indistinguishable from a slow-but-working server. Captured to a real
    log file instead (matching _spawn_frozen_policy_bot's own established log_path fix), and the
    process is checked for an early exit right after the startup grace period -- a dead server is
    now a real, immediate, loud RuntimeError naming the log file and printing its own tail,
    instead of a silent, misleading timeout much later."""
    log_dir = _server_log_dir or tempfile.gettempdir()
    log_path = os.path.join(log_dir, f"server_{port}.log")
    log_f = open(log_path, "w")
    args = [SERVER_BIN, "--deathmatch", "--fast-forward", "--port", str(port)]
    if _training_level_path:
        args += ["--level", _training_level_path]
    proc = subprocess.Popen(args, cwd=REPO_ROOT, stdout=log_f, stderr=subprocess.STDOUT)
    _spawned_procs.append(proc)
    time.sleep(0.5)  # real, minimal startup grace period -- server_net_init binds synchronously
    if proc.poll() is not None:
        log_f.close()
        with open(log_path) as f:
            tail = f.read()[-4000:]
        raise RuntimeError(
            f"shank_server (port {port}) exited immediately with code {proc.returncode} -- "
            f"see {log_path}. Last output:\n{tail}"
        )
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


def _run_evaluation_match(host, port, checkpoint_a, checkpoint_b, duration_seconds=EVAL_DURATION_SECONDS, kills_to=EVAL_KILLS_TO):
    """Real, minimal 1v1 evaluation match -- SHANKPIT's own necessary analog to BRAWLPIT's
    dedicated rl_evaluate.py (no PACKET_RESET_MATCH / match-boundary concept exists here to build
    a cleaner one on top of, see this module's own top-of-file doc comment). Spawns two real
    frozen_policy_bot.py processes on an isolated, --fast-forward server.

    S474: the real, primary stopping condition is now first-to-kills_to (BRAWLPIT's own real
    5-stock convention, not a single decisive kill) -- polls both bots' own periodically-
    rewritten report files (frozen_policy_bot.py's own --report-interval) and terminates both the
    moment either side reaches kills_to, rather than always waiting out the full duration_seconds.
    duration_seconds (+ EVAL_STARTUP_GRACE_SECONDS) stays a real, honest safety cap for the case
    neither side ever reaches kills_to (two very passive/weak policies) -- falls through to
    reading each bot's own final natural report in that case.

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

            def _read_kills(path):
                try:
                    with open(path) as f:
                        return json.load(f)["kills"]
                except (FileNotFoundError, json.JSONDecodeError, KeyError, OSError):
                    return None

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
            deadline = time.time() + duration_seconds + EVAL_STARTUP_GRACE_SECONDS
            early_kills = None  # (kills_a, kills_b) at the moment a real first-to-kills_to fires, or None
            while time.time() < deadline:
                if bot_a.poll() is not None and bot_b.poll() is not None:
                    break  # both already exited on their own (hit session_duration) -- read final reports below
                ka, kb = _read_kills(report_a), _read_kills(report_b)
                if (ka is not None and ka >= kills_to) or (kb is not None and kb >= kills_to):
                    early_kills = (ka or 0, kb or 0)
                    break
                time.sleep(EVAL_POLL_INTERVAL_SECONDS)

            if early_kills is not None:
                for p in (bot_a, bot_b):
                    p.terminate()
            for p in (bot_a, bot_b):
                try:
                    p.wait(timeout=5.0)
                except subprocess.TimeoutExpired:
                    p.kill()
                if p in _spawned_procs:
                    _spawned_procs.remove(p)
                # Flush+close the captured log file now that the process has exited, so _tail()
                # below reads everything the bot actually wrote, not a partially-buffered file.
                log_f = getattr(p, "_shankpit_log_file", None)
                if log_f is not None:
                    log_f.close()

            # S459-61, real, found-live gap: this function used to return a silent 0.5 for BOTH
            # a genuine 0-0 tie (both bots really did fight for the full window and neither
            # landed a kill -- a real, plausible outcome this early in training) and a bot
            # crash/report-write failure -- founder real-time: "i have 2 gens same elo seems
            # wrong" gave no way to tell which was actually happening from the training log
            # alone. Now prints which case it was, so the NEXT run's own log answers the
            # question directly instead of needing a guess.
            def _tail(path, n=15):
                try:
                    with open(path) as f:
                        lines = f.readlines()
                    return "".join(lines[-n:]).rstrip() or "(empty)"
                except OSError:
                    return "(log file missing)"

            if early_kills is not None:
                # A real early stop already told us the true, current kill counts at decision
                # time -- re-reading the report files after terminate() adds nothing (SIGTERM
                # gives frozen_policy_bot.py no chance to write a fresher one) and risks reading
                # nothing at all if the process died mid-write.
                kills_a, kills_b = early_kills
                print(f"    [eval] {os.path.basename(checkpoint_a)} kills={kills_a} vs "
                      f"{os.path.basename(checkpoint_b)} kills={kills_b} (early stop: reached {kills_to})", flush=True)
                note = f"first-to-{kills_to}: kills {kills_a}-{kills_b}"
            else:
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
                      f"{os.path.basename(checkpoint_b)} kills={kills_b} (both reports read fine, "
                      f"timed out before either reached {kills_to})", flush=True)
                note = f"timed out before first-to-{kills_to}: kills {kills_a}-{kills_b}"

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
    p.add_argument("--hec-token", default=os.environ.get("IDUNA_HEC_TOKEN"),
                   help="S474 (founder real-time: 'can we have more debugging in the heartbeat'). "
                        "If set (or IDUNA_HEC_TOKEN in the environment), every opponent selection, "
                        "eval result, and generation registration also pushes a real, structured "
                        "heartbeat event to IDUNA's unified logging backend (POST /services/"
                        "collector) at --registry-url, so a run's own history survives past an "
                        "ephemeral Colab session's stdout. A real, deliberate no-op (training keeps "
                        "running exactly as before) if this isn't set.")
    # Real, found-live fix (2026-09-17, founder: "the shankpit elos... the bottom 3 bots still
    # have 1500 that seems wrong"): default is now ON, with an explicit opt-out. S459-71 already
    # fixed colab_train.py's own wrapper to always pass this flag -- but this script itself, when
    # invoked directly (bypassing that wrapper), still defaulted to off. Every generation with no
    # prior lineage to compare against skips evaluation entirely (see the resume_generation logic
    # below) and keeps its default/inherited ELO forever -- confirmed live: 113 of 350 SHANKPIT
    # checkpoints (32%) stuck at exactly 1500, traced to fresh (non-resumed) launches. Resuming is
    # a no-op without --registry-url regardless of this default (see the guard just below).
    p.add_argument("--resume-from-registry", dest="resume_from_registry", action="store_true", default=True,
                   help="Default: on. Warm-starts each role from the newest checkpoint that role "
                        "already has in the shared registry instead of a fresh network -- a fresh "
                        "network never gets evaluated against anything (see module doc), so this "
                        "is what keeps every checkpoint's ELO real. No-op if --registry-url isn't "
                        "set. Use --no-resume-from-registry to opt out.")
    p.add_argument("--no-resume-from-registry", dest="resume_from_registry", action="store_false",
                   help="Opt out of the default-on resume behavior -- start every role from a "
                        "fresh network even if the registry already has checkpoints for it.")
    p.add_argument("--device", default=os.environ.get("SHANKPIT_RL_DEVICE", "cpu"),
                   help="stable_baselines3 device. Defaults to 'cpu' -- this pipeline's own tiny "
                        "MLP policy plus one real UDP round trip per environment step is "
                        "latency-bound, not compute-bound (same real measured finding BRAWLPIT's "
                        "own rl_train_packet.py already documents).")
    p.add_argument("--ent-coef", type=float, default=DEFAULT_ENT_COEF)
    p.add_argument("--max-episode-steps", type=int, default=1000)
    args = p.parse_args()

    registry_jwt = None
    registry_jwt_issued_at = 0.0
    if args.registry_url:
        if not args.registry_agent_secret:
            print("--registry-url was set but --registry-agent-secret (or IDUNA_AGENT_SECRET) "
                  "wasn't -- refusing to silently skip the registry push.")
            return 1
        registry_jwt = authenticate(args.registry_url, args.registry_agent_name, args.registry_agent_secret)
        registry_jwt_issued_at = time.time()
        print(f"Authenticated with the remote checkpoint registry at {args.registry_url}.")

    # S459-104, founder real-time: "DEFAULT FOR QUEUE SHOULD SET TRAINING LEVEL HAVE THE TRAINING
    # LEVEL OUTPUT IN THE COLAB SKRIP SO WE CAN SEE ITS WORKING" -- real gap: this orchestrator
    # never wired up the same is_default_queue level flag apps/server/src/main.c's own
    # queue_activate_match already uses, so setting "default for queue" in NOCK's SHANKPIT level
    # editor had zero effect on what bots actually trained against -- always the hardcoded
    # --deathmatch scene rotation. Public endpoint, no registry auth needed, so this runs
    # regardless of --registry-url. Printed plainly (not buried in a debug flag) precisely so it's
    # visible in the Colab log, per the founder's own explicit ask.
    global _training_level_path
    level_fetch_url = args.registry_url or os.environ.get("IDUNA_BASE_URL")
    if level_fetch_url:
        level_dest = os.path.join(args.output_dir, "_training_level.json")
        os.makedirs(args.output_dir, exist_ok=True)
        result = fetch_default_queue_level(level_fetch_url, level_dest)
        if result:
            level_name, level_id, box_count = result
            _training_level_path = level_dest
            print(f"TRAINING_LEVEL name={level_name!r} id={level_id} boxes={box_count} "
                  f"(is_default_queue in NOCK's SHANKPIT level editor) -- {level_dest}")
        else:
            print("TRAINING_LEVEL: no level is currently flagged is_default_queue (or the "
                  "registry was unreachable) -- falling back to the built-in --deathmatch scene "
                  "rotation, same as before this existed.")
    else:
        print("TRAINING_LEVEL: no --registry-url/IDUNA_BASE_URL set -- can't look up "
              "is_default_queue, using the built-in --deathmatch scene rotation.")

    if args.resume_from_registry and not args.registry_url:
        # Real, deliberate: resume defaults to True now (see the flag's own doc comment), so this
        # is simply "resuming is impossible without a registry" -- not an error. Forcing every
        # registry-less local/test run to pass --no-resume-from-registry just to avoid an abort
        # would be a real regression the default-flip should never have caused.
        args.resume_from_registry = False

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
    global _server_log_dir
    _server_log_dir = args.output_dir
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
        # S459-104, founder real-time: "can you build token refreshing in whenever it checks in
        # new models can you have it refresh the token it expires and then we train a bunch that
        # never gets checked in" -- authenticate() returns a real, short-lived (1hr) Bearer JWT,
        # obtained once at startup. A long-running training session (many generations of real
        # wall-clock training, latency-bound at one real UDP round trip per env step) can easily
        # outlive that hour -- every push_checkpoint/update_checkpoint_elo call after expiry was
        # silently swallowed (each call site's own "a registry outage must never crash a real,
        # in-progress training run" except-and-continue), so real training kept happening locally
        # but nothing ever reached the registry again for the rest of the run. Proactively
        # re-authenticate well before the real 1hr expiry rather than waiting to react to a 401.
        if registry_jwt and (time.time() - registry_jwt_issued_at) > REGISTRY_JWT_REFRESH_SECONDS:
            try:
                registry_jwt = authenticate(args.registry_url, args.registry_agent_name, args.registry_agent_secret)
                age_s = time.time() - registry_jwt_issued_at
                registry_jwt_issued_at = time.time()
                print(f"[gen {generation}] refreshed registry auth token (previous one was {age_s:.0f}s old)")
            except Exception as e:  # noqa: BLE001 -- a refresh failure must never crash a real, in-progress training run
                print(f"[gen {generation}] WARNING: registry auth token refresh failed ({e}), "
                      f"continuing with the existing token until it actually stops working")

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
                push_heartbeat(args.registry_url, args.hec_token, "opponent_chosen", {
                    "generation": generation, "role": role.value, "opponent_id": self_play_opponent_id,
                })
            else:
                print(f"[gen {generation}] {role.value}: no league members registered yet -- heuristic-only bootstrap", flush=True)
                push_heartbeat(args.registry_url, args.hec_token, "opponent_chosen", {
                    "generation": generation, "role": role.value, "opponent_id": None, "note": "heuristic-only bootstrap",
                })
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
            push_heartbeat(args.registry_url, args.hec_token, "generation_registered", {
                "generation": generation, "role": role.value, "member_id": member.id,
                "elo_inherited": league.get_elo(member.id), "reset": role in reset_roles,
            })

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
            # Real, found-live fix (2026-09-17, founder: "the shankpit elos... the bottom 3
            # bots still have 1500"): Main Exploiter resets to a fresh network every
            # --reset-every-n-generations (5, by default) and used to be registered with a
            # fresh inherited 1500 AND have evaluation skipped outright every single time --
            # confirmed live via the real registry data, main_exploiter sits stuck at 1500 for
            # 40% of its rows vs ~27% for the other two roles, exactly the ~1-in-5 rate resets
            # fire at. A reset network isn't untestable, though -- it has a real, natural
            # opponent already tracked (best_checkpoint_path[MAIN], the whole reason Main
            # Exploiter exists is to probe the current best Main), so evaluate against that
            # instead of skipping.
            if role in reset_roles:
                main_opponent_path = best_checkpoint_path.get(LeagueRole.MAIN)
                main_opponent_id = best_member_id.get(LeagueRole.MAIN)
                if main_opponent_path is None or main_opponent_id is None or main_opponent_id == member.id:
                    eval_notes[role] = "reset generation, no real Main checkpoint yet to evaluate against"
                    continue
                try:
                    elo_before = league.get_elo(member.id)
                    score_a, note = _run_evaluation_match(args.host, EVAL_PORT, checkpoint_paths[role], main_opponent_path)
                    eval_notes[role] = f"reset generation, vs current best Main: {note}"
                    league.record_match_result(member.id, main_opponent_id, score_a)
                    elo_after = league.get_elo(member.id)
                    print(f"[gen {generation}]   -> evaluated {role.value} (reset) vs current best Main: "
                          f"score_a={score_a} (local elo now {elo_after:.0f})")
                    push_heartbeat(args.registry_url, args.hec_token, "eval_result", {
                        "generation": generation, "role": role.value, "opponent_id": main_opponent_id,
                        "opponent_kind": "current_best_main_reset_eval", "score_a": score_a, "note": note,
                        "elo_before": elo_before, "elo_after": elo_after,
                    })
                except Exception as e:  # noqa: BLE001 -- an evaluation match failing must never crash real, in-progress training
                    eval_notes[role] = f"CRASH: reset-generation evaluation vs Main failed ({e})"
                    print(f"[gen {generation}]   -> WARNING: reset-generation evaluation for {role.value} failed ({e}), Elo unchanged")
                    push_heartbeat(args.registry_url, args.hec_token, "eval_result", {
                        "generation": generation, "role": role.value, "opponent_id": main_opponent_id,
                        "opponent_kind": "current_best_main_reset_eval", "error": str(e),
                    })
                continue
            if role not in prev_checkpoint_paths:
                eval_notes[role] = "no prior generation to evaluate against yet"
                continue
            try:
                score_a, note = _run_evaluation_match(args.host, EVAL_PORT, checkpoint_paths[role], prev_checkpoint_paths[role])
                eval_notes[role] = note
                league.record_match_result(member.id, prev_member_ids[role], score_a)
                new_elo, prev_elo = league.get_elo(member.id), league.get_elo(prev_member_ids[role])
                print(f"[gen {generation}]   -> evaluated {role.value} vs its own prior generation: "
                      f"score_a={score_a} (local elo now {new_elo:.0f} vs {prev_elo:.0f})")
                push_heartbeat(args.registry_url, args.hec_token, "eval_result", {
                    "generation": generation, "role": role.value, "opponent_id": prev_member_ids[role],
                    "opponent_kind": "prior_generation", "score_a": score_a, "note": note,
                    "elo_after": new_elo, "opponent_elo_after": prev_elo,
                })

                # S459-76, real, found-live gap: founder real-time "im a little concerned that
                # the elos of the generation 0 bots arent going up and down... can we make sure
                # the elos are set up to go up and down not just whatever the first elo into the
                # registry is?" record_match_result above just updated the PRIOR generation's own
                # real local Elo too (it's a real, symmetric two-sided update -- elo_update always
                # touches both members), but that prior generation was already pushed to the
                # remote registry BEFORE this evaluation ever ran, with whatever Elo it had at
                # push time (1500 for a real gen 0, since it had nothing to evaluate against yet).
                # Nothing ever pushed the UPDATE back -- so a checkpoint's own remote Elo was
                # permanently frozen at its push-time value forever, no matter how many later
                # generations evaluated against it afterward. Now real: patch the prior
                # generation's own already-registered row with its real, current post-match Elo.
                if registry_jwt and role in prev_remote_ids:
                    try:
                        update_checkpoint_elo(args.registry_url, registry_jwt, prev_remote_ids[role], prev_elo,
                                               eval_note=f"vs gen {generation}: score_a={1.0 - score_a}")
                        print(f"[gen {generation}]   -> updated prior generation's own remote elo "
                              f"(checkpoint id={prev_remote_ids[role]}, elo={prev_elo:.0f})")
                    except Exception as e:  # noqa: BLE001 -- a registry outage must never crash a real, in-progress training run
                        print(f"[gen {generation}]   -> WARNING: updating prior generation's remote elo failed ({e})")

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
                push_heartbeat(args.registry_url, args.hec_token, "eval_result", {
                    "generation": generation, "role": role.value, "opponent_id": prev_member_ids.get(role),
                    "opponent_kind": "prior_generation", "error": str(e),
                })

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
                push_heartbeat(args.registry_url, args.hec_token, "eval_result", {
                    "generation": generation, "role": role.value, "opponent_id": opponent_id,
                    "opponent_kind": "pfsp_sampled", "score_a": score_a, "note": _note,
                })
            except Exception as e:  # noqa: BLE001
                print(f"[gen {generation}]   -> WARNING: PFSP feedback match for {role.value} failed ({e})")
                push_heartbeat(args.registry_url, args.hec_token, "eval_result", {
                    "generation": generation, "role": role.value, "opponent_id": opponent_id,
                    "opponent_kind": "pfsp_sampled", "error": str(e),
                })

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
