#!/usr/bin/env python3
"""
scripts/rl_registry.py (S459-49/50) -- a real, minimal client for IDUNA's own remote RL
checkpoint registry (POST/GET /api/v1/shankpit-checkpoints, IDUNA/internal/shankpit/
checkpoint_store.go). Founder real-time: "bring in the bot registry affordances on NOCK all the
same... for shankpit" then "ensure we have the colab training skrip" -- a direct, faithful port
of BRAWLPIT/scripts/rl_registry.py's own real client, scoped to match SHANKPIT's own registry
(no weights_file/match-result -- see checkpoint_store.go's own doc comment for why).

scripts/rl_league.py's own LeagueManager is a real, working registry, but it's a LOCAL filesystem
directory shared only by processes on the SAME machine. This module is the network half: any
training location (this box, a Colab runtime, a future machine) authenticates as the real
SHANKPIT-RL M2M agent and pushes/pulls checkpoints through IDUNA.

Uses only the standard library (urllib) -- no `requests` dependency, matching
scripts/rl_env_packet.py's own established "minimal dependency footprint" discipline.
"""

import json
import mimetypes
import os
import urllib.error
import urllib.parse
import urllib.request
import uuid


def authenticate(base_url, agent_name, agent_secret):
    """POST /api/v1/auth/agent -- returns a real, short-lived (1hr) Bearer JWT. Same real M2M
    flow every other agent in this monorepo already uses."""
    body = json.dumps({"agent_name": agent_name, "agent_secret": agent_secret}).encode()
    req = urllib.request.Request(
        f"{base_url}/api/v1/auth/agent", data=body,
        headers={"Content-Type": "application/json"}, method="POST",
    )
    with urllib.request.urlopen(req, timeout=15) as resp:
        data = json.load(resp)
    return data["access_token"]


def _multipart_body(fields, files):
    """Real, minimal multipart/form-data encoder -- the standard library has no built-in one."""
    boundary = uuid.uuid4().hex
    parts = []
    for name, value in fields.items():
        parts.append(f"--{boundary}\r\n".encode())
        parts.append(f'Content-Disposition: form-data; name="{name}"\r\n\r\n'.encode())
        parts.append(f"{value}\r\n".encode())
    for field_name, filename, file_bytes in files:
        content_type = mimetypes.guess_type(filename)[0] or "application/octet-stream"
        parts.append(f"--{boundary}\r\n".encode())
        parts.append(
            f'Content-Disposition: form-data; name="{field_name}"; filename="{filename}"\r\n'
            f"Content-Type: {content_type}\r\n\r\n".encode()
        )
        parts.append(file_bytes)
        parts.append(b"\r\n")
    parts.append(f"--{boundary}--\r\n".encode())
    return b"".join(parts), f"multipart/form-data; boundary={boundary}"


def push_checkpoint(base_url, jwt, role, generation, elo, source_location, path, eval_note=""):
    """POST /api/v1/shankpit-checkpoints -- uploads one real checkpoint file + its metadata.
    Requires a JWT carrying the real shankpit.checkpoints.write permission (see IDUNA/migrations/
    truestore/202609150022_shankpit_rl_checkpoints.sql's own new SHANKPIT-RL agent). Returns the
    real registered Checkpoint dict (id/name/sha256/size_bytes/created_at).

    eval_note (S459-63, founder real-time: "how the fuck is my colab log gonna help it just says
    training") -- a real, plain-text summary of this generation's own evaluation match outcome
    (kill counts, or a crash reason), pushed alongside the checkpoint so it's visible through this
    same registry API without needing access to the training process's own stdout. Optional,
    empty by default (generation 0 has nothing to evaluate against yet)."""
    with open(path, "rb") as f:
        file_bytes = f.read()
    files = [("file", os.path.basename(path), file_bytes)]

    body, content_type = _multipart_body(
        {"role": role, "generation": generation, "elo": elo, "source_location": source_location, "eval_note": eval_note},
        files,
    )
    req = urllib.request.Request(
        f"{base_url}/api/v1/shankpit-checkpoints", data=body,
        headers={"Content-Type": content_type, "Authorization": f"Bearer {jwt}"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            return json.load(resp)
    except urllib.error.HTTPError as e:
        raise RuntimeError(f"push_checkpoint failed ({e.code}): {e.read().decode(errors='replace')}") from e


def list_checkpoints(base_url, role=None):
    """GET /api/v1/shankpit-checkpoints[?role=...] -- real, public, no auth needed (same trust
    level GET /api/v1/shankpit-levels already established)."""
    url = f"{base_url}/api/v1/shankpit-checkpoints"
    if role:
        url += f"?role={urllib.parse.quote(role)}"
    with urllib.request.urlopen(url, timeout=15) as resp:
        return json.load(resp)


def get_active_checkpoint(base_url):
    """GET /api/v1/shankpit-checkpoints/active (S459-62) -- real, public, no auth needed, same
    trust level list_checkpoints above already has. Returns the real Checkpoint dict, or None if
    no checkpoint has ever been activated (IDUNA returns a real, honest `null`, not a 404) --
    ops/shankpit-bot-pool.sh's own real, deliberate stopgap consumer: "can we update the QUEUE to
    use the active opponent until we have a league to queue against?" (founder real-time)."""
    url = f"{base_url}/api/v1/shankpit-checkpoints/active"
    with urllib.request.urlopen(url, timeout=15) as resp:
        return json.load(resp)


def download_checkpoint(base_url, checkpoint_id, dest_path):
    """GET /api/v1/shankpit-checkpoints/<id>/download -- real, public, streams the raw .zip
    bytes to `dest_path`. Returns dest_path for convenience."""
    url = f"{base_url}/api/v1/shankpit-checkpoints/{checkpoint_id}/download"
    with urllib.request.urlopen(url, timeout=60) as resp:
        data = resp.read()
    with open(dest_path, "wb") as f:
        f.write(data)
    return dest_path


if __name__ == "__main__":
    import argparse

    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)

    push_p = sub.add_parser("push", help="upload one checkpoint to the remote registry")
    push_p.add_argument("--base-url", default=os.environ.get("IDUNA_BASE_URL", "https://okemily.com"))
    push_p.add_argument("--agent-name", default=os.environ.get("IDUNA_AGENT_NAME", "SHANKPIT-RL"))
    push_p.add_argument("--agent-secret", default=os.environ.get("IDUNA_AGENT_SECRET"))
    push_p.add_argument("--role", required=True)
    push_p.add_argument("--generation", type=int, required=True)
    push_p.add_argument("--elo", type=float, required=True)
    push_p.add_argument("--source-location", required=True)
    push_p.add_argument("path")

    list_p = sub.add_parser("list", help="list checkpoints in the remote registry")
    list_p.add_argument("--base-url", default=os.environ.get("IDUNA_BASE_URL", "https://okemily.com"))
    list_p.add_argument("--role")

    pull_p = sub.add_parser("pull", help="download one checkpoint by id")
    pull_p.add_argument("--base-url", default=os.environ.get("IDUNA_BASE_URL", "https://okemily.com"))
    pull_p.add_argument("id", type=int)
    pull_p.add_argument("dest")

    active_p = sub.add_parser("active", help="print the real, currently-activated checkpoint's id (or nothing, exit 1, if none is set)")
    active_p.add_argument("--base-url", default=os.environ.get("IDUNA_BASE_URL", "https://okemily.com"))

    args = p.parse_args()

    if args.cmd == "push":
        if not args.agent_secret:
            raise SystemExit("--agent-secret (or IDUNA_AGENT_SECRET) is required")
        jwt = authenticate(args.base_url, args.agent_name, args.agent_secret)
        result = push_checkpoint(args.base_url, jwt, args.role, args.generation, args.elo,
                                  args.source_location, args.path)
        print(json.dumps(result, indent=2))
    elif args.cmd == "list":
        for c in list_checkpoints(args.base_url, args.role):
            print(f"id={c['id']:4d}  name={c.get('name', ''):28s}  role={c['role']:18s}  gen={c['generation']:3d}  "
                  f"elo={c['elo']:7.1f}  from={c['source_location']}")
    elif args.cmd == "pull":
        path = download_checkpoint(args.base_url, args.id, args.dest)
        print(f"downloaded -> {path}")
    elif args.cmd == "active":
        c = get_active_checkpoint(args.base_url)
        if c is None:
            raise SystemExit(1)  # real, honest "no active opponent set" -- a plain exit code a shell script can branch on
        print(c["id"])
