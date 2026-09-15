#!/usr/bin/env python3
"""
scripts/colab_train.py (S459-50) -- the real, single "drop into one Colab cell" bootstrap.
Founder real-time: "ensure we have the colab training skrip" -- a direct, faithful port of
BRAWLPIT/scripts/colab_train.py, scoped to SHANKPIT's own real pipeline (scripts/
rl_env_packet.py / rl_train_packet.py / rl_league.py / rl_registry.py, S459-45/46/48/49).

Paste this whole file's contents into one Colab cell and run it (or, once SHANKPIT is already
cloned somewhere, `!python3 scripts/colab_train.py`) -- it clones/updates the repo, builds the
real training binaries (bin/shank_server, bin/emily-bot), installs gymnasium/stable_baselines3,
authenticates against IDUNA, and kicks off a real training run via rl_train_packet.py, pushing the
final checkpoint to the shared SHANKPIT-RL registry (IDUNA/internal/shankpit/checkpoint_store.go)
when an agent secret is given.

Real, honest scope difference from BRAWLPIT's own colab_train.py: SHANKPIT's registry has no
--resume-from-registry warm-start yet (rl_train_packet.py always starts a fresh policy) and no
--num-envs parallel-rollout support yet -- both real, named, deferred gaps (see
docs/BOT_TRAINING_NORTHSTAR.md §9), not silently dropped from the port.

On "oauth into IDUNA to get the token used for training": this system's real token exchange for a
MACHINE (not a human) is IDUNA's M2M agent-secret grant (POST /api/v1/auth/agent) -- the same
mechanism every other automated agent in this monorepo uses, not a browser OAuth redirect.

Prereqs: a GitHub personal access token with `repo` read scope (this repo is private), and the
real SHANKPIT-RL agent secret from IDUNA/var/agent-secrets.env (IDUNA_SECRET_SHANKPIT_RL) if you
want this run to push to the shared registry -- leave the secret blank for a local-only smoke run.
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

    timesteps = os.environ.get("SHANKPIT_TOTAL_TIMESTEPS", "20000")
    out_path = os.environ.get("SHANKPIT_CHECKPOINT_OUT", "var/rl_checkpoints/ppo_shankpit_queue_colab.zip")
    cmd = [
        sys.executable, "scripts/rl_train_packet.py",
        "--port", os.environ.get("SHANKPIT_TRAIN_PORT", "17777"),
        "--timesteps", timesteps,
        "--max-episode-steps", os.environ.get("SHANKPIT_MAX_EPISODE_STEPS", "1000"),
        "--opponents", os.environ.get("SHANKPIT_OPPONENTS", "2"),
        "--out", out_path,
    ]
    if os.environ.get("SHANKPIT_FAST_FORWARD") == "1":
        cmd.append("--fast-forward")

    print("\nStarting real training -- this runs until --timesteps completes or the cell/runtime is stopped.", flush=True)
    _stream(cmd)

    if iduna_agent_secret:
        from rl_registry import push_checkpoint, authenticate as _auth
        jwt = _auth(IDUNA_BASE_URL, "SHANKPIT-RL", iduna_agent_secret)
        print(f"\nPushing {out_path} to the shared registry at {IDUNA_BASE_URL}...")
        result = push_checkpoint(
            IDUNA_BASE_URL, jwt,
            role=os.environ.get("SHANKPIT_ROLE", "main"),
            generation=int(os.environ.get("SHANKPIT_GENERATION", "1")),
            elo=float(os.environ.get("SHANKPIT_ELO", "1500")),
            source_location="colab",
            path=out_path,
        )
        print(f"Pushed -- registered as id={result['id']} name={result['name']}")
        print(f"View it in NOCK: {IDUNA_BASE_URL}/admin/nock#shankpit-ai-opponents")
    del iduna_agent_secret
    print(f"\nDone. Local checkpoint: {out_path}")


if __name__ == "__main__":
    main()
