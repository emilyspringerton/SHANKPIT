#!/usr/bin/env python3
"""local_nock.py -- dependency-free local stand-in for the IDUNA NOCK map tooling.

SHANKPIT's in-game editor already speaks a small HTTP contract for "live editing sessions"
(packages/world/edit_sync.h <-> IDUNA/internal/http/handlers/shankpit_edit_sessions.go). This
serves that same contract locally, so the cloud tools being unreachable stops nothing:

  * the browser editor here and the native client join the SAME session, so an edit made in either
    place shows up in the other (the client pushes avatar/spawner/box events and reloads the working
    level when the browser posts a "level" event);
  * levels are plain JSON files in levels/, one per map -- maps are code, reviewable and diffable;
  * every save is git add + git commit (no push: that's still a deliberate human step).

  python3 tools/local_nock.py --port 8787
  # browser: http://127.0.0.1:8787
  # game (join the session id the browser shows, or start one and paste it in):
  SHANKPIT_EDIT_SESSIONS_URL=http://127.0.0.1:8787/api/v1/shankpit-edit-sessions \
    ./bin/shank_lobby --edit-map levels/starter.json --edit-live

Wire contract (matches the Go handler, which the C client is written against):
  GET  /                                        browser editor
  GET  /api/levels                              {"levels":[{name,walls,bytes,...}]}
  GET  /api/levels/<name>                       one level (native export JSON)
  PUT  /api/levels/<name>                       save to levels/<name>.json (+ git commit)
  POST /api/v1/shankpit-edit-sessions           {"id":"<32 hex>","level_id":N,"level_name":"..","seq":0}
  GET  /api/v1/shankpit-edit-sessions/<id>?since=N&wait=S   long-poll {"seq":N,"events":[...]}
  POST /api/v1/shankpit-edit-sessions/<id>/events          {"source","kind","data"}
  PUT  /api/v1/shankpit-edit-sessions/<id>/level           working draft (browser pushes)
  GET  /api/v1/shankpit-edit-sessions/<id>/level           the native export the client loads

Sessions live in memory only (an editing room, not a record), exactly like IDUNA's: a restart ends
the room and both sides make a new one. Saving is what makes a level durable.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import secrets
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, parse_qs, urlparse

HERE_FIRST = os.path.dirname(os.path.abspath(__file__))
if HERE_FIRST not in sys.path:
    sys.path.insert(0, HERE_FIRST)
# The same binder the CLI runs. The point of importing it rather than reimplementing it here is that
# the editor and `tools/mixamo_clipset.py` cannot disagree about what a valid bundle is.
import mixamo_clipset  # noqa: E402

# A level name is one path segment, or TWO -- `lofts/loft_props_cafe` -- so the recursive loft tree in
# levels/lofts/ is editable here. Deliberately not a general path: no dot segments, no absolute paths,
# no deeper nesting, and path() re-checks containment after joining anyway.
SLUG_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}(?:/[A-Za-z0-9_-]{1,64})?$")
SESSION_PREFIX = "/api/v1/shankpit-edit-sessions"
LEVELS_PREFIX = "/api/levels"
CLIPSETS_PREFIX = "/api/clipsets"
MAX_BODY = 4 << 20          # a whole level draft; IDUNA's cap for the same reason
MAX_EVENT_BODY = 64 << 10   # one event is small; keep the log's per-entry cost bounded too
MAX_EVENTS = 2000
MAX_WAIT = 25
EVENT_KINDS = {
    "spawner", "avatar", "spawn_mode", "teleport",
    "box_add", "box_move", "box_size", "box_del", "vehicle_spawn", "hello", "level", "save",
}
HERE = os.path.dirname(os.path.abspath(__file__))


class BodyTooLarge(Exception):
    """Raised by Handler._body when Content-Length exceeds the cap (answered as 413)."""


def validate_level(doc):
    """The minimum a level must be to be usable.

    Either a `walls` array (an ordinary, playable level) OR an `objects` array (a COMPOSITION level:
    a level whose geometry is nothing but references to other levels -- that is exactly what the
    recursive loft tree in levels/lofts/ is made of). Both may be present; neither may be malformed.
    """
    if not isinstance(doc, dict):
        raise ValueError("level must be a JSON object")
    has_objects = "objects" in doc
    walls = doc.get("walls")
    if walls is None and not has_objects:
        raise ValueError('level needs a "walls" array (or an "objects" array for a composed level)')
    if walls is not None and not isinstance(walls, list):
        raise ValueError('"walls" must be an array')
    for i, w in enumerate(walls or []):
        if not isinstance(w, dict):
            raise ValueError(f"wall {i} is not an object")
        missing = [k for k in ("x", "y", "z", "sx", "sy", "sz") if k not in w]
        if missing:
            raise ValueError(f"wall {i} is missing {', '.join(missing)}")
    if has_objects:
        objs = doc.get("objects")
        if not isinstance(objs, list):
            raise ValueError('"objects" must be an array')
        for i, o in enumerate(objs):
            if not isinstance(o, dict):
                raise ValueError(f"object {i} is not an object")
            ref = o.get("ref_level_id")
            if isinstance(ref, bool) or not isinstance(ref, int) or ref <= 0:
                raise ValueError(f"object {i} needs a positive integer ref_level_id")
            for k in ("x", "y", "z"):
                v = o.get(k, 0)
                if isinstance(v, bool) or not isinstance(v, (int, float)):
                    raise ValueError(f"object {i} has a non-numeric {k}")
            if o.get("rot_y", 0) not in (0, 90, 180, 270):
                raise ValueError(f"object {i} rot_y must be 0/90/180/270 "
                                 "(the engine composes quarter turns only)")
    # The subtract modifier: cubes that cut themselves out of whatever level geometry they intersect
    # (elevator shafts through floors, stairwells through decks). The native loader resolves these at
    # parse time, so the editor only has to carry them faithfully -- but carrying a malformed one
    # through would be a hole that silently never appears, which is worse than a refused save. The
    # schema is the same shorthand a wall uses: centre x/y/z plus FULL extents sx/sy/sz.
    if "subtract" in doc:
        subs = doc.get("subtract")
        if not isinstance(subs, list):
            raise ValueError('"subtract" must be an array of cubes')
        for i, s in enumerate(subs):
            if not isinstance(s, dict):
                raise ValueError(f"subtract {i} is not an object")
            missing = [k for k in ("x", "y", "z", "sx", "sy", "sz") if k not in s]
            if missing:
                raise ValueError(f"subtract {i} is missing {', '.join(missing)}")
            for k in ("x", "y", "z", "sx", "sy", "sz"):
                v = s.get(k)
                if isinstance(v, bool) or not isinstance(v, (int, float)):
                    raise ValueError(f"subtract {i} has a non-numeric {k}")
            if s["sx"] <= 0 or s["sy"] <= 0 or s["sz"] <= 0:
                raise ValueError(f"subtract {i} needs positive sx/sy/sz (a cube with no volume cuts nothing)")
    return doc


class Session:
    """One live editing room: an append-only event log plus the browser's latest working draft."""

    def __init__(self, sid, level_id, level_name):
        self.id = sid
        self.level_id = level_id
        self.level_name = level_name
        self.events = []
        self.next_seq = 1
        self.draft = None
        self.draft_rev = 0
        self.created = self.touched = time.time()
        self.cond = threading.Condition()

    def snapshot(self, since):
        events = [e for e in self.events if e["seq"] > since]
        return {
            "id": self.id,
            "level_id": self.level_id,
            "level_name": self.level_name,
            "seq": self.next_seq - 1,
            "events": events,
            "level_rev": self.draft_rev,
        }

    def post(self, source, kind, data):
        with self.cond:
            event = {"seq": self.next_seq, "source": source, "kind": kind, "data": data}
            self.next_seq += 1
            self.events.append(event)
            if len(self.events) > MAX_EVENTS:
                del self.events[:-MAX_EVENTS]
            self.touched = time.time()
            self.cond.notify_all()
            return event

    def poll(self, since, wait):
        deadline = time.time() + wait
        with self.cond:
            while self.next_seq - 1 <= since and wait > 0 and time.time() < deadline:
                self.cond.wait(max(0.0, deadline - time.time()))
            self.touched = time.time()
            return self.snapshot(since)


class LevelStore:
    """Levels as files under levels/ -- git-tracked, one JSON per map, saved by committing."""

    def __init__(self, levels_dir, repo, commit=True):
        self.levels_dir = levels_dir
        self.repo = repo
        self.commit = commit

    def path(self, name):
        if not SLUG_RE.match(name or ""):
            raise ValueError("bad level name")
        p = os.path.join(self.levels_dir, *name.split("/")) + ".json"
        # both segments are slug-checked above, but resolve and confirm anyway: this function is what
        # every read and write goes through, and a path that escapes levels/ is never acceptable.
        root = os.path.realpath(self.levels_dir)
        if os.path.commonpath([root, os.path.realpath(os.path.dirname(p))]) != root:
            raise ValueError("level path escapes the levels directory")
        return p

    def names(self):
        """Levels directly under levels/, plus one level down (levels/lofts/*.json)."""
        if not os.path.isdir(self.levels_dir):
            return []
        out = []
        for fn in os.listdir(self.levels_dir):
            p = os.path.join(self.levels_dir, fn)
            if os.path.isfile(p) and fn.endswith(".json") and SLUG_RE.match(fn[:-5]):
                out.append(fn[:-5])
            elif os.path.isdir(p) and SLUG_RE.match(fn):
                for sub in os.listdir(p):
                    if sub.endswith(".json") and SLUG_RE.match(fn + "/" + sub[:-5]):
                        out.append(fn + "/" + sub[:-5])
        return sorted(out)

    def listing(self):
        out = []
        for name in self.names():
            p = self.path(name)
            try:
                with open(p, "r", encoding="utf-8") as f:
                    doc = json.load(f)
            except (OSError, ValueError):
                continue
            walls = doc.get("walls") if isinstance(doc, dict) else None
            objs = doc.get("objects") if isinstance(doc, dict) else None
            out.append({
                "name": name,
                "file": os.path.relpath(p, self.repo),
                "walls": len(walls) if isinstance(walls, list) else 0,
                "objects": len(objs) if isinstance(objs, list) else 0,
                "bytes": os.path.getsize(p),
            })
        return out

    def read(self, name):
        p = self.path(name)
        if not os.path.isfile(p):
            return None
        with open(p, "r", encoding="utf-8") as f:
            return f.read()

    def write(self, name, doc):
        validate_level(doc)
        p = self.path(name)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        tmp = p + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            f.write(json.dumps(doc, indent=1) + "\n")
        os.replace(tmp, p)
        return {"ok": True, "file": os.path.relpath(p, self.repo), "git": self._commit(p, name, doc)}

    def _commit(self, path, name, doc):
        if not self.commit:
            return {"committed": False, "detail": "commits disabled (--no-commit)"}
        rel = os.path.relpath(path, self.repo)
        walls = len(doc.get("walls") or [])
        objs = len(doc.get("objects") or [])
        what = f"{walls} wall{'s' if walls != 1 else ''}"
        if objs:
            what += f", {objs} level reference{'s' if objs != 1 else ''}"
        msg = f"level({name}): {what} from local NOCK"
        try:
            add = subprocess.run(["git", "-C", self.repo, "add", "--", rel],
                                 capture_output=True, text=True)
            if add.returncode != 0:
                return {"committed": False, "detail": (add.stderr or add.stdout).strip()[:400]}
            status = subprocess.run(["git", "-C", self.repo, "status", "--porcelain", "--", rel],
                                    capture_output=True, text=True)
            if not status.stdout.strip():
                return {"committed": False, "detail": "no changes to commit"}
            c = subprocess.run(["git", "-C", self.repo, "commit", "-m", msg, "--", rel],
                               capture_output=True, text=True)
            if c.returncode != 0:
                detail = (c.stderr or c.stdout).strip()[:400]
                return {"committed": False, "detail": detail or "commit failed"}
            head = subprocess.run(["git", "-C", self.repo, "rev-parse", "--short", "HEAD"],
                                  capture_output=True, text=True).stdout.strip()
            return {"committed": True, "commit": head, "message": msg}
        except FileNotFoundError:
            return {"committed": False, "detail": "git not found"}


class ClipsetStore:
    """Mixamo/GOLDENBAND clips as a committed bundle: assets/goldenband/clipsets/<name>/.

    Same shape as LevelStore -- a file is the record, saving is committing -- because that is the
    founder's own "import the assets and then commit them back up to git" for this repo. The bundle
    is the thirteen-name contract directory the engine loads (packages/simulation/anim_clipset.h),
    not a copy of anyone's raw FBX: the binder validates and renames, and only its output is tracked.

    A raw .fbx/.glb staging directory is deliberately NOT here. Adobe's terms allow baking Mixamo
    into a game but not redistributing the editable source assets, so the import path starts from
    .gband clips that this repo already owns; a local NOCK that wrote raw exports into a public
    tree would be the exact thing those terms forbid.
    """

    def __init__(self, repo, assets_dir, commit=True):
        self.repo = repo
        self.assets_dir = assets_dir
        self.commit = commit

    def clipsets_dir(self):
        return os.path.join(self.assets_dir, "clipsets")

    def available(self):
        """Clip stems that have BOTH halves. The binary is channel-blind; without the manifest a
        clip cannot be sampled by joint at all, so offering it would only produce a failed import."""
        if not os.path.isdir(self.assets_dir):
            return []
        stems = sorted(fn[:-len(".gband")] for fn in os.listdir(self.assets_dir)
                       if fn.endswith(".gband"))
        return [s for s in stems if os.path.isfile(os.path.join(self.assets_dir, s + ".gband.json"))]

    def sets(self):
        base = self.clipsets_dir()
        if not os.path.isdir(base):
            return []
        out = []
        for name in sorted(os.listdir(base)):
            d = os.path.join(base, name)
            if not os.path.isdir(d):
                continue
            bound = [fn[:-len(".gband")] for fn in os.listdir(d) if fn.endswith(".gband")]
            out.append({
                "name": name,
                "bound": len(bound),
                "total": len(mixamo_clipset.CONTRACT),
                "missing": [s for s in mixamo_clipset.CONTRACT if s not in bound],
            })
        return out

    def bind(self, name, mapping, auto, partial, mesh=None):
        """Returns (result, error). The binder's own messages are what the editor shows."""
        if not SLUG_RE.match(name or ""):
            return None, "bad clip set name (letters, digits, _ and -, max 64)"
        if auto:
            found, missing, ambiguous = mixamo_clipset.auto_map(self.available())
            if ambiguous:
                return None, "ambiguous: " + "; ".join(
                    "%s could be %s" % (s, " or ".join(h)) for s, h in ambiguous)
            if missing and not partial:
                return None, "no clip for: " + ", ".join(missing)
            mapping = found
        if not isinstance(mapping, dict) or not mapping:
            return None, "nothing mapped: pick a clip for at least one state, or tick auto"
        if mesh is not None:
            mesh = str(mesh).strip()
            if mesh and (not SLUG_RE.match(mesh) or not os.path.isfile(os.path.join(self.assets_dir, mesh + ".gskel"))):
                return None, "no %s.gskel in assets/goldenband -- pick a mesh that exists" % (mesh or "?")
        out = os.path.join(self.clipsets_dir(), name)
        errors, warnings = mixamo_clipset.write_bundle(mapping, self.assets_dir, out,
                                                       allow_partial=partial, mesh=mesh or None)
        if errors:
            return None, "; ".join(errors)
        return {
            "ok": True, "name": name, "dir": os.path.relpath(out, self.repo),
            "bound": len(mapping), "warnings": warnings, "git": self._commit(out, name, len(mapping)),
        }, None

    def _commit(self, path, name, bound):
        if not self.commit:
            return {"committed": False, "detail": "commits disabled (--no-commit)"}
        rel = os.path.relpath(path, self.repo)
        msg = f"asset(clipset): {name} bound to the 13-clip contract from local NOCK ({bound} clips)"
        try:
            add = subprocess.run(["git", "-C", self.repo, "add", "--", rel], capture_output=True, text=True)
            if add.returncode != 0:
                return {"committed": False, "detail": (add.stderr or add.stdout).strip()[:400]}
            status = subprocess.run(["git", "-C", self.repo, "status", "--porcelain", "--", rel],
                                    capture_output=True, text=True)
            if not status.stdout.strip():
                return {"committed": False, "detail": "no changes to commit"}
            c = subprocess.run(["git", "-C", self.repo, "commit", "-m", msg, "--", rel],
                               capture_output=True, text=True)
            if c.returncode != 0:
                return {"committed": False, "detail": (c.stderr or c.stdout).strip()[:400] or "commit failed"}
            head = subprocess.run(["git", "-C", self.repo, "rev-parse", "--short", "HEAD"],
                                  capture_output=True, text=True).stdout.strip()
            return {"committed": True, "commit": head, "message": msg}
        except FileNotFoundError:
            return {"committed": False, "detail": "git not found"}


def rebuild_loft_city(repo, commit=True):
    """Flatten the recursive loft tree into the zombies city, then refresh the live baked level.

    This is the half of composition authoring that a level editor cannot give you by saving a file:
    a bay edited in NOCK has no geometry in the city until the tree is flattened, and a flattened
    city has no effect on a connected game until the baked default is refreshed. Both are the same
    two commands a human runs (`make build-lofts` + `tools/update_baked_level.py`), so the button and
    the command line cannot drift apart.
    """
    steps = [
        [sys.executable, os.path.join(repo, "tools", "build_loft_city.py")],
        [sys.executable, os.path.join(repo, "tools", "update_baked_level.py"),
         "--level", "var/zombie/nextown_zombies.json"],
    ]
    out = []
    for cmd in steps:
        r = subprocess.run(cmd, cwd=repo, capture_output=True, text=True)
        text = (r.stdout or "").strip() or (r.stderr or "").strip()
        if text:
            out.append(text)
        if r.returncode != 0:
            return {"ok": False, "output": "\n".join(out),
                    "detail": "failed: " + " ".join(os.path.basename(c) for c in cmd[1:])}
    result = {"ok": True, "output": "\n".join(out)}
    if not commit:
        result["git"] = {"committed": False, "detail": "commits disabled (--no-commit)"}
        return result
    files = ["var/zombie/nextown_zombies.json", "packages/world/baked_levels_gen.h", "levels/lofts"]
    try:
        subprocess.run(["git", "-C", repo, "add", "--"] + files, capture_output=True, text=True)
        status = subprocess.run(["git", "-C", repo, "status", "--porcelain", "--"] + files,
                                capture_output=True, text=True)
        if not status.stdout.strip():
            result["git"] = {"committed": False, "detail": "no changes to commit"}
            return result
        msg = "level(lofts): flatten the composed tree into the city and refresh the live default"
        c = subprocess.run(["git", "-C", repo, "commit", "-m", msg, "--"] + files,
                           capture_output=True, text=True)
        if c.returncode != 0:
            result["git"] = {"committed": False,
                             "detail": (c.stderr or c.stdout).strip()[:400] or "commit failed"}
            return result
        head = subprocess.run(["git", "-C", repo, "rev-parse", "--short", "HEAD"],
                              capture_output=True, text=True).stdout.strip()
        result["git"] = {"committed": True, "commit": head, "message": msg}
    except FileNotFoundError:
        result["git"] = {"committed": False, "detail": "git not found"}
    return result


class State:
    def __init__(self, store, editor_html, verbose=False, clips=None):
        self.store = store
        self.editclipsets = clips
        self.editor_html = editor_html
        self.sessions = {}
        self.lock = threading.Lock()
        self.verbose = verbose

    def create_session(self, level_id, level_name):
        sid = secrets.token_hex(16)
        s = Session(sid, level_id, level_name)
        with self.lock:
            self.sessions[sid] = s
        return s

    def get_session(self, sid):
        with self.lock:
            return self.sessions.get(sid)


class Handler(BaseHTTPRequestHandler):
    server_version = "local-nock/1.0"

    # -- plumbing -------------------------------------------------------------------------
    def log_message(self, fmt, *args):
        if self.server.state.verbose:
            sys.stderr.write("  %s\n" % (fmt % args))

    def _send(self, status, body, ctype="application/json"):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _json(self, status, obj):
        self._send(status, json.dumps(obj))

    def _error(self, status, msg):
        self._json(status, {"error": msg})

    def _body(self, limit=MAX_BODY):
        try:
            n = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            n = 0
        if n <= 0:
            return {}
        if n > limit:
            # Drain what the peer is still sending BEFORE answering: replying 413 and closing the
            # socket mid-upload makes the client fail with a broken pipe instead of reading the
            # status, which is a real bug found by this file's own oversized-body check. Bounded
            # by the declared Content-Length, which is all the peer said it would send.
            remaining = n
            while remaining > 0:
                chunk = self.rfile.read(min(1 << 16, remaining))
                if not chunk:
                    break
                remaining -= len(chunk)
            # A distinct exception so handlers can answer 413 rather than a vague 400: the same
            # code the PARENA runner returns for the same request.
            raise BodyTooLarge(f"body too large (limit {limit} bytes)")
        raw = self.rfile.read(n)
        return json.loads(raw.decode("utf-8")) if raw else {}

    def _session_route(self, path):
        """Returns (id, tail) for /api/v1/shankpit-edit-sessions/<id>[/tail], else (None, None)."""
        if not path.startswith(SESSION_PREFIX + "/"):
            return None, None
        rest = path[len(SESSION_PREFIX) + 1:]
        sid, _, tail = rest.partition("/")
        return sid, tail

    # -- GET ------------------------------------------------------------------------------
    def do_GET(self):
        u = urlparse(self.path)
        path = u.path
        st = self.server.state

        if path in ("/", "/editor.html"):
            return self._send(200, st.editor_html, "text/html; charset=utf-8")

        if path == "/api/local-nock":
            return self._json(200, {
                "levels_dir": os.path.relpath(st.store.levels_dir, st.store.repo),
                "sessions": len(st.sessions),
                "commit": st.store.commit,
            })

        if path == LEVELS_PREFIX:
            return self._json(200, {"levels": st.store.listing()})

        if path == CLIPSETS_PREFIX:
            return self._json(200, {
                "contract": mixamo_clipset.CONTRACT,
                "clips": st.editclipsets.available() if st.editclipsets else [],
                "sets": st.editclipsets.sets() if st.editclipsets else [],
                "package": os.path.relpath(st.editclipsets.assets_dir, st.editclipsets.repo)
                           if st.editclipsets else "",
            })

        if path.startswith(LEVELS_PREFIX + "/"):
            name = unquote(path[len(LEVELS_PREFIX) + 1:])
            if not SLUG_RE.match(name):
                return self._error(400, "bad level name")
            text = st.store.read(name)
            if text is None:
                return self._error(404, "no such level")
            return self._send(200, text)

        sid, tail = self._session_route(path)
        if sid is not None:
            s = st.get_session(sid)
            if s is None:
                return self._error(404, "no such edit session (expired, or this server restarted)")
            if tail == "":
                q = parse_qs(u.query)
                try:
                    since = int(q.get("since", ["0"])[0])
                    wait = min(int(q.get("wait", ["0"])[0]), MAX_WAIT)
                except ValueError:
                    return self._error(400, "since/wait must be integers")
                return self._json(200, s.poll(since, wait))
            if tail == "events":
                return self._error(405, "method not allowed")
            if tail == "level":
                if s.draft is not None:
                    return self._send(200, json.dumps(s.draft))
                text = st.store.read(s.level_name) if SLUG_RE.match(s.level_name or "") else None
                if text is None:
                    return self._error(404, "this session has no level yet (open one in the editor)")
                return self._send(200, text)
            return self._error(404, "unknown session route")

        return self._error(404, "not found")

    # -- POST -----------------------------------------------------------------------------
    def do_POST(self):
        path = urlparse(self.path).path
        st = self.server.state

        if path.startswith(LEVELS_PREFIX):
            return self._error(405, "method not allowed")

        if path == CLIPSETS_PREFIX:
            if not st.editclipsets:
                return self._error(503, "clip sets are not configured")
            try:
                body = self._body()
            except BodyTooLarge as e:
                return self._error(413, str(e))
            except ValueError as e:
                return self._error(400, str(e))
            try:
                result, error = st.editclipsets.bind(
                    str(body.get("name") or "")[:64],
                    body.get("map"),
                    bool(body.get("auto")),
                    bool(body.get("partial")),
                    body.get("mesh"),
                )
            except OSError as e:
                return self._error(500, f"cannot write clip set: {e}")
            if error:
                return self._error(400, error)
            return self._json(200, result)

        if path == "/api/lofts/rebuild":
            try:
                result = rebuild_loft_city(st.store.repo, commit=st.store.commit)
            except OSError as e:
                return self._error(500, f"cannot rebuild: {e}")
            return self._json(200 if result.get("ok") else 500, result)

        if path == SESSION_PREFIX:
            try:
                body = self._body()
            except BodyTooLarge as e:
                return self._error(413, str(e))
            except ValueError as e:
                return self._error(400, str(e))
            name = str(body.get("level_name") or "")[:120]
            try:
                level_id = int(body.get("level_id") or 0)
            except (TypeError, ValueError):
                level_id = 0
            s = st.create_session(level_id, name)
            return self._json(201, {"id": s.id, "level_id": s.level_id,
                                    "level_name": s.level_name, "seq": 0})

        sid, tail = self._session_route(path)
        if sid is not None and tail == "events":
            s = st.get_session(sid)
            if s is None:
                return self._error(404, "no such edit session")
            try:
                body = self._body(limit=MAX_EVENT_BODY)
            except BodyTooLarge as e:
                return self._error(413, str(e))
            except ValueError as e:
                return self._error(400, str(e))
            source = str(body.get("source") or "")[:16]
            kind = str(body.get("kind") or "")[:16]
            if source not in ("shankpit", "nock"):
                return self._error(400, 'source must be "shankpit" or "nock"')
            if kind not in EVENT_KINDS:
                return self._error(400, "unknown event kind")
            data = body.get("data")
            if data is None:
                data = {}
            event = s.post(source, kind, data)
            return self._json(200, event)

        return self._error(404, "not found")

    # -- PUT ------------------------------------------------------------------------------
    def do_PUT(self):
        path = urlparse(self.path).path
        st = self.server.state

        if path.startswith(LEVELS_PREFIX + "/"):
            name = unquote(path[len(LEVELS_PREFIX) + 1:])
            if not SLUG_RE.match(name):
                return self._error(400, "bad level name (letters, digits, - and _ only)")
            try:
                doc = self._body()
                result = st.store.write(name, doc)
            except BodyTooLarge as e:
                return self._error(413, str(e))
            except ValueError as e:
                return self._error(400, str(e))
            except OSError as e:
                return self._error(500, f"cannot write level: {e}")
            return self._json(200, result)

        sid, tail = self._session_route(path)
        if sid is not None and tail == "events":
            return self._error(405, "method not allowed")
        if sid is not None and tail == "level":
            s = st.get_session(sid)
            if s is None:
                return self._error(404, "no such edit session")
            try:
                doc = validate_level(self._body())
            except BodyTooLarge as e:
                return self._error(413, str(e))
            except ValueError as e:
                return self._error(400, str(e))
            with s.cond:
                s.draft = doc
                s.draft_rev += 1
                s.touched = time.time()
                rev = s.draft_rev
            return self._json(200, {"rev": rev})

        return self._error(404, "not found")


def build_server(port=8787, host="127.0.0.1", levels_dir=None, repo=None, commit=True,
                 verbose=False, assets_dir=None):
    """Create (but do not start) the server -- used by main() and by the integration test."""
    repo = os.path.abspath(repo or os.getcwd())
    levels_dir = os.path.abspath(levels_dir or os.path.join(repo, "levels"))
    assets_dir = os.path.abspath(assets_dir or os.path.join(repo, "assets", "goldenband"))
    with open(os.path.join(HERE, "nock_editor.html"), "r", encoding="utf-8") as f:
        editor_html = f.read()
    store = LevelStore(levels_dir, repo, commit=commit)
    clips = ClipsetStore(repo, assets_dir, commit=commit)
    server = ThreadingHTTPServer((host, port), Handler)
    server.daemon_threads = True
    server.state = State(store, editor_html, verbose=verbose, clips=clips)
    return server


def main(argv=None):
    ap = argparse.ArgumentParser(description="Local NOCK map tools: browser editor + live session "
                                             "server for SHANKPIT's in-game editor.")
    ap.add_argument("--port", type=int, default=8787)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--levels-dir", default=None, help="where level JSON lives (default ./levels)")
    ap.add_argument("--repo", default=None, help="git repo the level saves commit into (default cwd)")
    ap.add_argument("--no-commit", action="store_true", help="save files without git commit")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    server = build_server(port=args.port, host=args.host, levels_dir=args.levels_dir,
                          repo=args.repo, commit=not args.no_commit, verbose=not args.quiet)
    port = server.server_address[1]
    levels = os.path.relpath(server.state.store.levels_dir, server.state.store.repo)
    print(f"local NOCK on http://{args.host}:{port}   levels: {levels}/   "
          f"commit: {'off' if args.no_commit else 'on'}")
    print()
    print("  browser:  open that URL, start a live session, then join it from the game:")
    print(f"            SHANKPIT_EDIT_SESSIONS_URL=http://{args.host}:{port}{SESSION_PREFIX} \\")
    print("              ./bin/shank_lobby --edit-map levels/starter.json --edit-live")
    print("            (or paste the game's session id into the editor and press Join)")
    print()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
