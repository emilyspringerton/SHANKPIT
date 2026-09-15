#!/usr/bin/env python3
"""
scripts/colab_train.py (S459-50, updated S459-59) -- the real, single "drop into one Colab cell"
bootstrap. Founder real-time: "ensure we have the colab training skrip" -- a direct, faithful port
of BRAWLPIT/scripts/colab_train.py, scoped to SHANKPIT's own real pipeline (scripts/
rl_env_packet.py / rl_train_packet.py / rl_league.py / rl_registry.py, S459-45/46/48/49).

Paste this whole file's contents into one Colab cell and run it (or, once SHANKPIT is already
cloned somewhere, `!python3 scripts/colab_train.py`) -- it clones/updates the repo, builds the
real training binaries (bin/shank_server, bin/emily-bot), installs gymnasium/stable_baselines3,
authenticates against IDUNA, and kicks off a real training run via rl_train_packet.py.

REAL, FOUND, LIVE CORRECTION (S459-59): this file previously invoked rl_train_packet.py with a
stale single-agent CLI (--port/--opponents/--out) left over from before S459-54 rewrote that
script into the real 3-role (Main/Main Exploiter/League Exploiter) self-play league orchestrator
(--league-dir/--output-dir/--registry-url/etc, no --port/--opponents/--out at all anymore) --
running the old cell as written would fail immediately with an argparse error, never training
anything. Fixed here: this cell now runs the real league orchestrator directly, which trains all
3 roles together and pushes each generation's checkpoints to the shared registry itself (no
separate push_checkpoint call needed afterward, unlike the old single-agent flow this replaces).
--resume-from-registry (real, warm-starts every role from its own newest registry checkpoint) is
also real and live now, unlike this doc's own previous "not built yet" claim -- see
rl_train_packet.py's own --resume-from-registry for the real contract.

On "oauth into IDUNA to get the token used for training": this system's real token exchange for a
MACHINE (not a human) is IDUNA's M2M agent-secret grant (POST /api/v1/auth/agent) -- the same
mechanism every other automated agent in this monorepo uses, not a browser OAuth redirect.

Prereqs: a GitHub personal access token with `repo` read scope (this repo is private), and the
real SHANKPIT-RL agent secret from IDUNA/var/agent-secrets.env (IDUNA_SECRET_SHANKPIT_RL) if you
want this run to push to the shared registry -- leave the secret blank for a local-only smoke run
(no registry push, --resume-from-registry unavailable).
"""

import getpass
import os
import subprocess
import sys

REPO_URL_TEMPLATE = "https://{token}@github.com/emilyspringerton/SHANKPIT.git"
IDUNA_BASE_URL = os.environ.get("IDUNA_BASE_URL", "https://okemily.com")


def _stream(cmd, env=None):
    """Real, found-live fix ported directly from BRAWLPIT/scripts/colab_train.py's own _stream
    (S439): a bare subprocess.run(cmd) inheriting raw OS file descriptors does not reliably show
    up in Colab/Jupyter cell output. Pipes the child's stdout+stderr and re-emits each line
    through a real Python print(..., flush=True) call instead."""
    proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True, bufsize=1)
    for line in proc.stdout:
        print(line, end="", flush=True)
    proc.wait()
    if proc.returncode != 0:
        raise subprocess.CalledProcessError(proc.returncode, cmd)
    return proc.returncode


def _run(cmd, **kwargs):
    print(f"$ {' '.join(cmd)}", flush=True)
    _stream(cmd, **kwargs)


def _bootstrap_repo(github_token):
    """Clones SHANKPIT if it isn't here yet, or forces it to the real latest remote state if it
    already is -- makes re-running this exact cell in the same Colab runtime (e.g. after a
    crash) safe, not just a first-run script."""
    if os.path.isdir("SHANKPIT"):
        print("SHANKPIT already present -- forcing it to the real latest remote state.")
        _run(["git", "-C", "SHANKPIT", "fetch", "origin", "master"])
        _run(["git", "-C", "SHANKPIT", "reset", "--hard", "origin/master"])
    else:
        _run(["git", "clone", REPO_URL_TEMPLATE.format(token=github_token), "SHANKPIT"])
    os.chdir("SHANKPIT")
    _run(["git", "log", "--oneline", "-1"])


GO_VERSION = "1.23.4"  # real, matches or exceeds go.mod's own `go 1.21` floor


def _ensure_go():
    """Real, found-live fix: unlike BRAWLPIT (pure C, no Go dependency at all -- this is why the
    gap wasn't caught by mirroring BRAWLPIT's own colab_train.py), SHANKPIT's `make emily-bot`
    needs a real Go toolchain, and Colab's base image ships none (`go: not found`, confirmed live
    against a real Colab run). Installs the official upstream tarball rather than `apt-get install
    golang-go` -- Ubuntu's own packaged Go is routinely too old for a `go.mod` floor this recent.
    A real, idempotent check-first: skips the download entirely if a real `go` binary already
    satisfies go.mod's own floor (e.g. a re-run in the same still-alive Colab runtime)."""
    try:
        out = subprocess.run(["go", "version"], capture_output=True, text=True, check=True).stdout
        print(f"go already installed: {out.strip()}")
        return
    except (FileNotFoundError, subprocess.CalledProcessError):
        pass
    print(f"Installing Go {GO_VERSION}...")
    tarball = f"go{GO_VERSION}.linux-amd64.tar.gz"
    _run(["wget", "-q", f"https://go.dev/dl/{tarball}", "-O", f"/tmp/{tarball}"])
    _run(["tar", "-C", "/usr/local", "-xzf", f"/tmp/{tarball}"])
    os.environ["PATH"] = "/usr/local/go/bin:" + os.environ.get("PATH", "")
    _run(["go", "version"])


def _bootstrap_build():
    # Colab's own base image already ships gcc/build-essential -- real, harmless no-op safety net.
    _run(["apt-get", "-qq", "update"])
    _run(["apt-get", "-qq", "install", "-y", "build-essential", "wget"])
    _ensure_go()
    # Real SHANKPIT build targets (Makefile) -- server + the real Go bot client used as training
    # opponents, both needed by rl_train_packet.py.
    _run(["make", "server"])
    _run(["make", "emily-bot"])  # PATH already carries /usr/local/go/bin from _ensure_go above (env=None inherits os.environ by default)
    _run([sys.executable, "-m", "pip", "install", "-q", "gymnasium", "stable-baselines3"])


def main():
    github_token = os.environ.get("GITHUB_TOKEN") or getpass.getpass("GitHub personal access token (repo read scope): ")
    iduna_agent_secret = os.environ.get("IDUNA_AGENT_SECRET") or getpass.getpass(
        "SHANKPIT-RL agent secret (blank to skip the shared registry -- local-only run): "
    )

    _bootstrap_repo(github_token)
    del github_token  # don't keep the token in memory longer than the clone needs it
    _bootstrap_build()

    sys.path.insert(0, "scripts")

    if iduna_agent_secret:
        from rl_registry import authenticate, list_checkpoints
        jwt = authenticate(IDUNA_BASE_URL, "SHANKPIT-RL", iduna_agent_secret)
        print(f"Authenticated with the shared registry at {IDUNA_BASE_URL} -- got a real, short-lived Bearer JWT.")
        print("-- current registry standings (before this run adds anything) --")
        by_role = {}
        for c in list_checkpoints(IDUNA_BASE_URL):
            by_role.setdefault(c["role"], []).append(c)
        for role in ("main", "main_exploiter", "league_exploiter"):
            newest = sorted(by_role.get(role, []), key=lambda c: -c["generation"])[:5]
            for c in newest:
                print(f"  id={c['id']:4d}  {c['role']:18s} gen={c['generation']:3d}  elo={c['elo']:7.1f}  {c.get('name', '')}")
        del jwt
    else:
        print("No agent secret given -- this run will train locally only and NOT join the shared registry.")

    # Real 3-role self-play league orchestrator (S459-54) -- trains Main/Main Exploiter/League
    # Exploiter together in one run, generation by generation, and pushes each generation's 3
    # checkpoints to the shared registry itself (when --registry-url/secret are set) -- no
    # separate push_checkpoint call needed here, unlike the old single-agent flow this replaces.
    # S459-70, founder real-time: "it only goes like 5 generations can you make it go like 100?"
    # -- generation count is real-timesteps-driven, not a separate knob: rl_train_packet.py's own
    # main loop runs until total_timesteps is exhausted, producing one generation per
    # --save-freq(=4096, its own real default) chunk -- 20000/4096 rounds to the real 5
    # generations being seen. 409600 = 100 * 4096, a real default sized for "100 generations" at
    # that same per-generation training amount, not a guess. Real, honest cost this trades for:
    # at this box's own live-measured ~4-5 min/generation, 100 generations is a genuinely long
    # run (multiple hours), well past a free-tier Colab session's own real idle/hard-cap limits --
    # if the runtime disconnects partway through, --resume-from-registry (now always on when
    # pushing to the registry, see S459-71 below) picks back up from each role's own latest
    # pushed checkpoint on the next run instead of losing progress.
    output_dir = os.environ.get("SHANKPIT_RL_OUTPUT_DIR", "var/rl_checkpoints/colab")
    cmd = [
        sys.executable, "scripts/rl_train_packet.py",
        "--total-timesteps", os.environ.get("SHANKPIT_TOTAL_TIMESTEPS", "409600"),
        "--max-episode-steps", os.environ.get("SHANKPIT_MAX_EPISODE_STEPS", "1000"),
        "--output-dir", output_dir,
        "--league-dir", os.environ.get("SHANKPIT_LEAGUE_DIR", "league_data"),
        "--heuristic-opponents", os.environ.get("SHANKPIT_HEURISTIC_OPPONENTS", "1"),
    ]
    pushing_to_registry = bool(iduna_agent_secret)
    if pushing_to_registry:
        cmd += [
            "--registry-url", IDUNA_BASE_URL,
            "--registry-agent-name", "SHANKPIT-RL",
            "--registry-agent-secret", iduna_agent_secret,
            "--registry-source-location", "colab",
        ]
        # S459-71, real, found-live correction: this used to be opt-in behind
        # SHANKPIT_RESUME_FROM_REGISTRY=1 -- founder real-time: "i should always have that in the
        # skrip - i said just like brawlpit thas how brawlpit works please fix it". Checked
        # BRAWLPIT/scripts/colab_train.py directly: it always passes --resume-from-registry
        # unconditionally whenever pushing to the registry, no separate flag -- this now matches
        # that exactly. Warm-starts every role from its own newest registry checkpoint (real PPO
        # weights, not just an Elo number) so this runtime continues the SAME shared league other
        # runs have been training, instead of a fresh random network every time.
        cmd.append("--resume-from-registry")

    print("\nStarting real league training -- this runs until --total-timesteps completes per "
          "role, or the cell/runtime is stopped.", flush=True)
    _stream(cmd)

    del iduna_agent_secret
    print(f"\nDone. Local checkpoints: {output_dir}/")
    if pushing_to_registry:
        print(f"View the league in NOCK: {IDUNA_BASE_URL}/admin/nock#shankpit-ai-opponents")


if __name__ == "__main__":
    main()
