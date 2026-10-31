#!/usr/bin/env python3
"""test_clipset_import.py -- local NOCK's "import clips + commit" affordance, over real HTTP.

Runs the actual server on an ephemeral port over a throwaway git repo whose assets/goldenband holds
copies of this repo's real .gband clips, then drives the two endpoints the editor's panel calls. What
is being checked is the editor's whole promise: a bundle appears on disk, it is committed, and an
import that would produce a broken bundle is refused with the reason the artist needs.

Run: python3 tools/test_clipset_import.py     (from the repo root)
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import local_nock  # noqa: E402

ASSETS = os.path.join(ROOT, "assets", "goldenband")
CHECKS = 0
FAILS = 0


def check(cond, what):
    global CHECKS, FAILS
    CHECKS += 1
    if cond:
        print("ok    " + what)
    else:
        FAILS += 1
        print("FAIL  " + what)


def call(base, method, path, body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(base + path, data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return r.status, json.loads(r.read() or b"null")
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"null")


def copy_pair(src_stem, dst_dir, dst_stem):
    for ext in (".gband", ".gband.json"):
        shutil.copyfile(os.path.join(ASSETS, src_stem + ext), os.path.join(dst_dir, dst_stem + ext))


def main():
    if not os.path.isdir(ASSETS):
        print("SKIP  no assets/goldenband in this checkout")
        return 0
    repo = tempfile.mkdtemp(prefix="clipset_nock_")
    server = None
    try:
        subprocess.run(["git", "init", "-q"], cwd=repo, check=True)
        subprocess.run(["git", "-C", repo, "config", "user.name", "clipset test"], check=True)
        subprocess.run(["git", "-C", repo, "config", "user.email", "clipset@localhost"], check=True)
        assets = os.path.join(repo, "assets", "goldenband")
        os.makedirs(assets)
        copy_pair("Mike_Idle", assets, "mixamo_idle")
        copy_pair("Mike_Walk", assets, "mixamo_walk")
        copy_pair("Mike_Hello", assets, "mixamo_hello")

        server = local_nock.build_server(port=0, levels_dir=os.path.join(repo, "levels"),
                                         repo=repo, commit=True, assets_dir=assets)
        base = "http://127.0.0.1:%d" % server.server_address[1]
        threading.Thread(target=server.serve_forever, daemon=True).start()

        print("== local NOCK: what the editor sees ==")
        status, doc = call(base, "GET", "/api/clipsets")
        check(status == 200, "GET /api/clipsets answers")
        check(len(doc["contract"]) == 13 and doc["contract"][0] == "anim_idle",
              "the panel is built from the 13-name contract the engine asks for")
        check(doc["clips"] == ["mixamo_hello", "mixamo_idle", "mixamo_walk"],
              "only clips with BOTH the binary and its manifest are offered")
        check(doc["sets"] == [], "no sets before the first import")
        check(doc["package"] == os.path.join("assets", "goldenband"), "the package path is reported, not guessed")

        print("== import: an incomplete rig is refused until you say otherwise ==")
        status, res = call(base, "POST", "/api/clipsets", {"name": "mike", "auto": True})
        check(status == 400 and "no clip for" in res["error"], "auto-map refuses a rig missing states")
        check(not os.path.isdir(os.path.join(assets, "clipsets", "mike")), "and writes nothing")

        status, res = call(base, "POST", "/api/clipsets", {"name": "mike", "auto": True, "partial": True})
        check(status == 200, "allow-partial accepts it: %s" % (res.get("error") if isinstance(res, dict) else res))
        check(res["bound"] == 2, "the two clips the names actually match were bound (idle, walk)")
        check(res["git"]["committed"] is True and res["git"]["commit"], "and the bundle is committed")
        log = subprocess.run(["git", "-C", repo, "log", "--oneline"], capture_output=True, text=True).stdout
        check("clipset" in log, "the commit message names the clip set, so git log is readable")
        check(os.path.isfile(os.path.join(assets, "clipsets", "mike", "anim_idle.gband")),
              "anim_idle.gband is on disk under the contract name")
        idle_manifest = json.load(open(os.path.join(assets, "clipsets", "mike", "anim_idle.gband.json")))
        check(idle_manifest["contract_state"] == "anim_idle" and idle_manifest["loop"] is True,
              "the manifest carries the contract state and its loop policy")

        print("== import: a complete, explicitly mapped set ==")
        mapping = {
            "anim_idle": "mixamo_idle", "anim_walk": "mixamo_walk", "anim_run": "mixamo_walk",
            "anim_crouch_idle": "mixamo_idle", "anim_crouch_walk": "mixamo_walk",
            "anim_jump_start": "mixamo_hello", "anim_in_air": "mixamo_hello",
            "anim_slide": "mixamo_hello", "anim_weapon_fire": "mixamo_hello",
            "anim_reload": "mixamo_hello", "anim_melee_swing": "mixamo_hello",
            "anim_ability": "mixamo_hello", "anim_climb_loop": "mixamo_hello",
        }
        status, res = call(base, "POST", "/api/clipsets", {"name": "mike_full", "map": mapping})
        check(status == 200 and res["bound"] == 13, "explicit mapping binds all thirteen")
        check(res["warnings"] and "share the clip" in res["warnings"][0],
              "reusing a clip for several states is reported, not hidden")
        status, doc = call(base, "GET", "/api/clipsets")
        sets = {s["name"]: s for s in doc["sets"]}
        check(sets["mike_full"]["bound"] == 13 and sets["mike_full"]["missing"] == [],
              "the listing says the set is complete")
        check("anim_run" in sets["mike"]["missing"], "and that the other set is not")

        print("== refusals the editor must show, not swallow ==")
        status, res = call(base, "POST", "/api/clipsets", {"name": "../escape", "auto": True})
        check(status == 400 and "bad clip set name" in res["error"], "a path-ish name is refused")

        status, res = call(base, "POST", "/api/clipsets", {"name": "empty"})
        check(status == 400 and "nothing mapped" in res["error"], "an empty import is refused")

        copy_pair("Mike_Idle", assets, "tampered")
        p = os.path.join(assets, "tampered.gband")
        data = bytearray(open(p, "rb").read())
        data[-1] ^= 0xFF
        open(p, "wb").write(data)
        status, res = call(base, "POST", "/api/clipsets", {"name": "bad", "map": {"anim_idle": "tampered"}})
        check(status == 400 and "content_hash" in res["error"],
              "a clip whose data no longer matches its hash is refused")
        check(not os.path.isdir(os.path.join(assets, "clipsets", "bad")), "and no half-bundle is left behind")

        print("== the same import twice is not a second commit ==")
        status, res = call(base, "POST", "/api/clipsets", {"name": "mike_full", "map": mapping})
        check(status == 200 and res["git"]["committed"] is False,
              "re-importing identical content reports 'no changes to commit'")
    finally:
        if server is not None:
            server.shutdown()
            server.server_close()
        shutil.rmtree(repo, ignore_errors=True)

    print("\n%d checks, %d failed" % (CHECKS, FAILS))
    return 0 if FAILS == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
