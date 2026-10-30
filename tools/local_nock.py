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

SLUG_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}$")
SESSION_PREFIX = "/api/v1/shankpit-edit-sessions"
LEVELS_PREFIX = "/api/levels"
MAX_BODY = 4 << 20          # a whole level draft; IDUNA's cap for the same reason
MAX_EVENT_BODY = 64 << 10   # one event is small; keep the log's per-entry cost bounded too
MAX_EVENTS = 2000
MAX_WAIT = 25
EVENT_KINDS = {
    "spawner", "avatar", "spawn_mode", "teleport",
    "box_add", "box_move", "box_size", "box_del", "hello", "level", "save",
}
HERE = os.path.dirname(os.path.abspath(__file__))


class BodyTooLarge(Exception):
    """Raised by Handler._body when Content-Length exceeds the cap (answered as 413)."""


def validate_level(doc):
    """The minimum a level must have to be usable: an object with a walls array of boxes."""
    if not isinstance(doc, dict):
        raise ValueError("level must be a JSON object")
    walls = doc.get("walls")
    if not isinstance(walls, list):
        raise ValueError('level needs a "walls" array')
    for i, w in enumerate(walls):
        if not isinstance(w, dict):
            raise ValueError(f"wall {i} is not an object")
        missing = [k for k in ("x", "y", "z", "sx", "sy", "sz") if k not in w]
        if missing:
            raise ValueError(f"wall {i} is missing {', '.join(missing)}")
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
        return os.path.join(self.levels_dir, name + ".json")

    def names(self):
        if not os.path.isdir(self.levels_dir):
            return []
        return sorted(fn[:-5] for fn in os.listdir(self.levels_dir)
                      if fn.endswith(".json") and SLUG_RE.match(fn[:-5]))

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
            out.append({
                "name": name,
                "file": os.path.relpath(p, self.repo),
                "walls": len(walls) if isinstance(walls, list) else 0,
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
        os.makedirs(self.levels_dir, exist_ok=True)
        p = self.path(name)
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
        msg = f"level({name}): {walls} wall{'s' if walls != 1 else ''} from local NOCK"
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


class State:
    def __init__(self, store, editor_html, verbose=False):
        self.store = store
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
                 verbose=False):
    """Create (but do not start) the server -- used by main() and by the integration test."""
    repo = os.path.abspath(repo or os.getcwd())
    levels_dir = os.path.abspath(levels_dir or os.path.join(repo, "levels"))
    with open(os.path.join(HERE, "nock_editor.html"), "r", encoding="utf-8") as f:
        editor_html = f.read()
    store = LevelStore(levels_dir, repo, commit=commit)
    server = ThreadingHTTPServer((host, port), Handler)
    server.daemon_threads = True
    server.state = State(store, editor_html, verbose=verbose)
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
