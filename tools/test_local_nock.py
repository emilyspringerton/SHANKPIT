#!/usr/bin/env python3
"""test_local_nock.py -- integration test for tools/local_nock.py.

Runs the real server on an ephemeral port over a throwaway git repo and exercises the wire contract
the native client and the browser editor both use. No browser needed, no network beyond loopback.

  make test-local-nock
"""
import json
import os
import subprocess
import sys
import tempfile
import threading
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import local_nock  # noqa: E402

LEVEL = {
    "name": "testmap", "width": 40, "height": 20, "depth": 40,
    "ground_plane_enabled": True, "ground_plane_squares": 4,
    "materials": [{"name": "brick", "shader_name": "standard", "specular": 0.04,
                   "shininess": 6, "friction": 0.3}],
    "walls": [{"x": 0, "y": 2, "z": 0, "sx": 8, "sy": 4, "sz": 2,
               "r": 0.6, "g": 0.6, "b": 0.65, "friction": 0.8, "material": "brick"}],
    "spawners": [{"id": 1, "x": 0, "y": 1, "z": 4, "yaw": 180, "team": -1}],
}


def call(base, method, path, body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(base + path, data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def json_call(base, method, path, body=None):
    status, raw = call(base, method, path, body)
    return status, (json.loads(raw) if raw else None)


def main():
    repo = tempfile.mkdtemp(prefix="local_nock_repo_")
    subprocess.run(["git", "init", "-q"], cwd=repo, check=True)
    subprocess.run(["git", "-C", repo, "config", "user.name", "local nock test"], check=True)
    subprocess.run(["git", "-C", repo, "config", "user.email", "local-nock@localhost"], check=True)

    server = local_nock.build_server(port=0, levels_dir=os.path.join(repo, "levels"),
                                     repo=repo, commit=True, verbose=False)
    port = server.server_address[1]
    base = f"http://127.0.0.1:{port}"
    threading.Thread(target=server.serve_forever, daemon=True).start()
    ok = 0

    try:
        # 1. the editor page is served
        status, raw = call(base, "GET", "/")
        assert status == 200 and b"local NOCK" in raw and b"canvas" in raw, "editor page"
        print("PASS editor page served")
        ok += 1

        # 2. an empty level list, then save a level: file on disk + git commit
        status, doc = json_call(base, "GET", "/api/levels")
        assert status == 200 and doc["levels"] == [], "empty listing"
        status, res = json_call(base, "PUT", "/api/levels/testmap", LEVEL)
        assert status == 200 and res["git"]["committed"] is True, f"save+commit: {res}"
        assert res["git"]["commit"], "commit hash"
        path = os.path.join(repo, "levels", "testmap.json")
        assert os.path.isfile(path), "level file written"
        log = subprocess.run(["git", "-C", repo, "log", "--oneline"], capture_output=True,
                             text=True).stdout
        assert "level(testmap)" in log, f"git log: {log!r}"
        print("PASS save writes levels/testmap.json and commits it")
        ok += 1

        # 3. list + read back through the API (what the editor/browser does)
        status, doc = json_call(base, "GET", "/api/levels")
        assert doc["levels"][0]["name"] == "testmap" and doc["levels"][0]["walls"] == 1
        status, raw = call(base, "GET", "/api/levels/testmap")
        assert status == 200 and json.loads(raw)["walls"][0]["sx"] == 8
        print("PASS level listing and read-back")
        ok += 1

        # 4. saving the same content again is not a new commit
        status, res = json_call(base, "PUT", "/api/levels/testmap", LEVEL)
        assert status == 200 and res["git"]["committed"] is False
        print("PASS identical save does not create an empty commit")
        ok += 1

        # 5. invalid levels are refused, nothing written
        status, res = json_call(base, "PUT", "/api/levels/broken", {"nope": 1})
        assert status == 400 and "walls" in res["error"]
        assert not os.path.exists(os.path.join(repo, "levels", "broken.json"))
        status, res = json_call(base, "PUT", "/api/levels/bad%20name", LEVEL)
        assert status == 400
        print("PASS invalid levels rejected (missing walls, bad name)")
        ok += 1

        # 6. session create -- 32 hex chars, the shape edit_sync_create checks for
        status, sess = json_call(base, "POST", "/api/v1/shankpit-edit-sessions",
                                 {"level_id": 0, "level_name": "testmap"})
        assert status == 201 and len(sess["id"]) == 32, sess
        sid = sess["id"]
        print("PASS session create returns a 32-hex id")
        ok += 1

        # 7. the native client's own event shapes (source shankpit) come back on poll
        for kind, data in (("hello", {"client": "shankpit"}),
                           ("avatar", {"x": 1.5, "y": 1, "z": 2.5, "yaw": 90, "pitch": 0}),
                           ("spawner", {"x": 3, "y": 1, "z": 4, "yaw": 45}),
                           ("box_add", {"id": 100001, "x": 5, "y": 2, "z": 5,
                                        "sx": 4, "sy": 4, "sz": 4}),
                           ("save", {})):
            status, _ = json_call(base, "POST", f"/api/v1/shankpit-edit-sessions/{sid}/events",
                                  {"source": "shankpit", "kind": kind, "data": data})
            assert status == 200, kind
        status, snap = json_call(base, "GET",
                                 f"/api/v1/shankpit-edit-sessions/{sid}?since=0&wait=0")
        kinds = [e["kind"] for e in snap["events"]]
        assert kinds == ["hello", "avatar", "spawner", "box_add", "save"], kinds
        assert snap["seq"] == 5
        status, snap = json_call(base, "GET",
                                 f"/api/v1/shankpit-edit-sessions/{sid}?since=4&wait=0")
        assert [e["kind"] for e in snap["events"]] == ["save"], "since filter"
        print("PASS events post/poll with since filtering")
        ok += 1

        # 8. unknown event kind and source are rejected (a room is not a message bus)
        status, res = json_call(base, "POST", f"/api/v1/shankpit-edit-sessions/{sid}/events",
                                {"source": "nock", "kind": "rm -rf", "data": {}})
        assert status == 400
        print("PASS unknown event kinds rejected")
        ok += 1

        # 9. the browser pushes a working draft; the client fetches it (live reload path)
        draft = dict(LEVEL)
        draft["walls"] = LEVEL["walls"] + [{"x": 4, "y": 2, "z": 4, "sx": 2, "sy": 2, "sz": 2}]
        status, res = json_call(base, "PUT", f"/api/v1/shankpit-edit-sessions/{sid}/level", draft)
        assert status == 200 and res["rev"] == 1, res
        status, raw = call(base, "GET", f"/api/v1/shankpit-edit-sessions/{sid}/level")
        served = json.loads(raw)
        assert status == 200 and len(served["walls"]) == 2, served
        status, res = json_call(base, "PUT", f"/api/v1/shankpit-edit-sessions/{sid}/level", draft)
        assert res["rev"] == 2
        print("PASS working draft push/fetch (browser -> game live reload)")
        ok += 1

        # 10. a session with no draft falls back to its saved level, then 404s when absent
        status, sess2 = json_call(base, "POST", "/api/v1/shankpit-edit-sessions",
                                  {"level_id": 0, "level_name": "testmap"})
        status, raw = call(base, "GET", f"/api/v1/shankpit-edit-sessions/{sess2['id']}/level")
        assert status == 200 and len(json.loads(raw)["walls"]) == 1, "saved-level fallback"
        status, sess3 = json_call(base, "POST", "/api/v1/shankpit-edit-sessions",
                                  {"level_id": 0, "level_name": "no-such-map"})
        status, res = json_call(base, "GET", f"/api/v1/shankpit-edit-sessions/{sess3['id']}/level")
        assert status == 404
        print("PASS level fetch falls back to the saved file, 404 when there is none")
        ok += 1

        # 11. long-poll returns promptly once an event lands
        status, sess4 = json_call(base, "POST", "/api/v1/shankpit-edit-sessions",
                                  {"level_id": 0, "level_name": "testmap"})
        sid4 = sess4["id"]
        result = {}

        def poll():
            result["r"] = json_call(base, "GET",
                                    f"/api/v1/shankpit-edit-sessions/{sid4}?since=0&wait=10")

        t = threading.Thread(target=poll, daemon=True)
        t.start()
        json_call(base, "POST", f"/api/v1/shankpit-edit-sessions/{sid4}/events",
                  {"source": "nock", "kind": "hello", "data": {"client": "nock"}})
        t.join(timeout=5)
        status, snap = result["r"]
        assert status == 200 and [e["kind"] for e in snap["events"]] == ["hello"]
        print("PASS long-poll wakes on a new event")
        ok += 1

        # 12. unknown session ids are 404 (the client treats that as 'session lost')
        status, _ = json_call(base, "GET", "/api/v1/shankpit-edit-sessions/" + "0" * 32 + "?since=0")
        assert status == 404
        print("PASS unknown session is 404")
        ok += 1

    finally:
        server.shutdown()
        server.server_close()

    print(f"\nlocal_nock ok ({ok} checks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
