#!/usr/bin/env python3
"""test_mixamo_clipset.py -- the binder against the repo's real .gband assets.

Every fixture here is a real GOLDENBAND clip (Mike/Leela/Stan/George, copied and renamed to
Mixamo-ish stems); nothing is synthesized, so a change in the real asset format breaks this test
rather than being papered over by a mock.

Run: python3 tools/test_mixamo_clipset.py      (from the repo root)
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, "tools", "mixamo_clipset.py")
ASSETS = os.path.join(ROOT, "assets", "goldenband")

# A normal Mixamo-ish export set, one clip per contract state.
STEMS = {
    "anim_idle": "mixamo_idle",
    "anim_walk": "mixamo_walk",
    "anim_run": "mixamo_sprint",
    "anim_crouch_idle": "mixamo_crouch_idle",
    "anim_crouch_walk": "mixamo_crouch_walk",
    "anim_jump_start": "mixamo_jump_start",
    "anim_in_air": "mixamo_in_air",
    "anim_slide": "mixamo_slide",
    "anim_weapon_fire": "mixamo_fire",
    "anim_reload": "mixamo_reload",
    "anim_melee_swing": "mixamo_swing",
    "anim_ability": "mixamo_ability",
    "anim_climb_loop": "mixamo_climb",
}

checks = 0
failures = 0


def check(cond, what):
    global checks, failures
    checks += 1
    if cond:
        print("ok    " + what)
    else:
        failures += 1
        print("FAIL  " + what)


def run(args):
    return subprocess.run([sys.executable, TOOL] + args, capture_output=True, text=True)


def copy_pair(src_stem, dst_dir, dst_stem):
    for ext in (".gband", ".gband.json"):
        shutil.copyfile(os.path.join(ASSETS, src_stem + ext), os.path.join(dst_dir, dst_stem + ext))


def main():
    if not os.path.isdir(ASSETS):
        print("SKIP  no assets/goldenband in this checkout")
        return 0
    tmp = tempfile.mkdtemp(prefix="clipset_test_")
    try:
        rig = os.path.join(tmp, "rig")
        os.makedirs(rig)
        # One real clip per state; Mike's own clips carry the four poses a robot actually has, and
        # the rest are filled from Mike/Leela so the set is 13 real clips on two skeletons -- the
        # skeleton check below is what keeps that honest.
        for state, stem in STEMS.items():
            src = "Mike_Idle" if stem in ("mixamo_idle", "mixamo_crouch_idle") else "Mike_Walk"
            if stem in ("mixamo_sprint", "mixamo_jump_start", "mixamo_in_air", "mixamo_slide"):
                src = "Mike_Walk"
            if stem in ("mixamo_fire", "mixamo_reload", "mixamo_swing", "mixamo_ability", "mixamo_climb"):
                src = "Mike_Hello"
            if stem in ("mixamo_crouch_walk",):
                src = "Mike_Walk"
            if stem in ("mixamo_crouch_idle",):
                src = "Mike_Idle"
            copy_pair(src, rig, stem)

        print("== binder: auto mapping from real clips ==")
        out = os.path.join(tmp, "out")
        r = run(["--assets", rig, "--auto", "--rig", "mixamo", "--out", out])
        check(r.returncode == 0, "auto-mapping a 13-clip rig succeeds (exit %d) %s" % (r.returncode, r.stderr.strip()[:160]))
        produced = sorted(f for f in os.listdir(out) if f.endswith(".gband"))
        check(len(produced) == 13, "the bundle contains exactly 13 clips")
        check(produced == sorted(s + ".gband" for s in STEMS), "every file is named after the contract state")

        print("== binder: manifests carry the contract, not just a rename ==")
        idle = json.load(open(os.path.join(out, "anim_idle.gband.json")))
        swing = json.load(open(os.path.join(out, "anim_melee_swing.gband.json")))
        src_idle = json.load(open(os.path.join(rig, "mixamo_idle.gband.json")))
        check(idle["channels"] == src_idle["channels"], "channel order is preserved verbatim (it is the data layout)")
        check(idle["content_hash"] == src_idle["content_hash"], "content hash is preserved (the clip is unchanged)")
        check(idle["contract_state"] == "anim_idle" and idle["source_clip"] == "mixamo_idle",
              "the manifest names the contract state and the clip it came from")
        check(idle["loop"] is True and idle["loop_points"]["end_tick"] == idle["duration_ticks"],
              "a posture loops over its full duration")
        check(swing["loop"] is False and swing["loop_points"]["end_tick"] == 0,
              "an action is marked one-shot, which is what anim_rules.prn says")
        check(idle["tick_rate"] == src_idle["tick_rate"] and idle["duration_ticks"] == src_idle["duration_ticks"],
              "rates come from the binary, not from the tool")

        print("== binder: refusals (each of these would be a silent bad animation) ==")
        partial = dict(STEMS)
        del partial["anim_ability"]
        pmap = os.path.join(tmp, "partial.json")
        json.dump(partial, open(pmap, "w"))
        r = run(["--assets", rig, "--map", pmap, "--out", os.path.join(tmp, "o2")])
        check(r.returncode == 1 and "anim_ability" in r.stderr, "a missing state is refused and named")

        r = run(["--assets", rig, "--map", pmap, "--out", os.path.join(tmp, "o3"), "--allow-partial"])
        check(r.returncode == 0 and len(os.listdir(os.path.join(tmp, "o3"))) == 24,
              "--allow-partial writes the 12 mapped states (binary + manifest each)")

        # Corrupt one clip's channel data without touching its hash.
        broke = os.path.join(tmp, "broke")
        shutil.copytree(rig, broke)
        p = os.path.join(broke, "mixamo_ability.gband")
        data = bytearray(open(p, "rb").read())
        data[-1] ^= 0xFF
        open(p, "wb").write(data)
        r = run(["--assets", broke, "--auto", "--rig", "mixamo", "--out", os.path.join(tmp, "o4")])
        check(r.returncode == 1 and "content_hash" in r.stderr, "edited channel data is caught by its own hash")

        # Two rigs in one bundle: one clip from a different skeleton.
        mixed = os.path.join(tmp, "mixed")
        shutil.copytree(rig, mixed)
        copy_pair("Leela_Idle", mixed, "mixamo_ability")
        r = run(["--assets", mixed, "--auto", "--rig", "mixamo", "--out", os.path.join(tmp, "o5")])
        check(r.returncode == 1 and "different skeletons" in r.stderr, "clips from two rigs are refused")

        # A truncated binary.
        short = os.path.join(tmp, "short")
        shutil.copytree(rig, short)
        p = os.path.join(short, "mixamo_slide.gband")
        open(p, "wb").write(open(p, "rb").read()[:100])
        r = run(["--assets", short, "--auto", "--rig", "mixamo", "--out", os.path.join(tmp, "o6")])
        check(r.returncode == 1, "a truncated binary is refused")

        print("== binder: ambiguity is an error, not a coin flip ==")
        amb = os.path.join(tmp, "amb")
        os.makedirs(amb)
        copy_pair("Mike_Idle", amb, "idle_a")
        copy_pair("Mike_Idle", amb, "idle_b")
        r = run(["--assets", amb, "--auto", "--out", os.path.join(tmp, "o7")])
        check(r.returncode == 2 and "ambiguous" in r.stderr, "two equal candidates stop the import")

        print("== binder: dry run ==")
        r = run(["--assets", rig, "--auto", "--rig", "mixamo", "--out", os.path.join(tmp, "o8"), "--dry-run"])
        check(r.returncode == 0 and not os.path.isdir(os.path.join(tmp, "o8")), "a dry run validates and writes nothing")

        print("== binder: shared clips are allowed but reported ==")
        shared = dict(STEMS)
        shared["anim_run"] = "mixamo_walk"  # a rig with no run cycle reuses its walk
        smap = os.path.join(tmp, "shared.json")
        json.dump(shared, open(smap, "w"))
        r = run(["--assets", rig, "--map", smap, "--out", os.path.join(tmp, "o9")])
        check(r.returncode == 0 and "share the clip" in r.stderr,
              "reusing one clip for two states warns instead of failing")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print("\n%d checks, %d failed" % (checks, failures))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
