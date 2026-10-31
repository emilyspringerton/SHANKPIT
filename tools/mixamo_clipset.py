#!/usr/bin/env python3
"""mixamo_clipset.py -- bind a rig's clips to the thirteen-name animation contract.

The contract is packages/simulation/anim_state.h's clip names (anim_idle .. anim_climb_loop),
whose rules live in PARENA/stdlib/shankpit/anim_rules.prn. Until now nothing produced a bundle
that satisfied it: the shipped .gband assets are named after the Mixamo clip they came from
(UAL1_Standard_Walk_Loop, Mike_Idle, ...), and the engine loads clips by contract name.

What this does, and deliberately does NOT do:

  it does NOT parse FBX/GLB. A real Mixamo conversion (resample curves to a uniform tick rate,
  emit channel data, hash it) already happened for every .gband in this repo; re-implementing a
  DCC importer here would be a second, worse one. This is the BINDING stage: given a directory of
  real .gband + .gband.json pairs and a mapping, produce <out>/<contract-name>.gband(+.json) and
  refuse the bundle if anything about it would fail at load or at runtime.

What it validates, all of which would otherwise surface as a silently-wrong animation:

  * every contract name is present exactly once (a missing one is a hard error naming it),
  * the binary is a real .gband: magic, version, exact length, non-zero duration/channels,
  * the manifest agrees with the binary: tick_rate, duration_ticks, len(channels) == num_channels,
  * the manifest's content_hash is the sha256 of the actual channel data (a clip edited on disk
    without rehashing is caught here rather than sampled as garbage),
  * every clip shares ONE skeleton_hash -- mixing rigs in a bundle is how a shoulder ends up on
    a knee, and the engine cannot see it because channels are matched by name,
  * loop metadata matches the rules: the postures loop (idle/walk/run/crouch/in_air), the actions
    do not. A looping reload is the classic bug this catches at import time.

Usage:
  tools/mixamo_clipset.py --assets assets/goldenband --map clipset.json --out var/clipsets/mike
  tools/mixamo_clipset.py --assets assets/goldenband --auto --rig Mike --out var/clipsets/mike

Exit: 0 = bundle written, 1 = bundle refused (reason printed), 2 = bad usage.
"""
import argparse
import hashlib
import json
import os
import shutil
import struct
import sys

# The contract, in anim_rules.prn's own state order. Keep this in step with anim_state.h.
CONTRACT = [
    "anim_idle", "anim_walk", "anim_run", "anim_crouch_idle", "anim_crouch_walk",
    "anim_jump_start", "anim_in_air", "anim_slide", "anim_weapon_fire", "anim_reload",
    "anim_melee_swing", "anim_ability", "anim_climb_loop",
]
# anim_rules.prn's anim-state-loops: 0..4 and 6 (IN_AIR) loop; 5 (JUMP_START) and 7..12 are one-shots.
LOOPING = {"anim_idle", "anim_walk", "anim_run", "anim_crouch_idle", "anim_crouch_walk", "anim_in_air"}

# --auto keyword table. Order matters: the first pattern that matches wins, so the specific
# phrases come before the generic ones ("crouch idle" before "idle").
AUTO_RULES = [
    ("anim_crouch_idle", ("crouchidle", "crouch_idle", "crouch idle", "crouchingidle")),
    ("anim_crouch_walk", ("crouchwalk", "crouch_walk", "crouch walk", "crouchingwalk")),
    ("anim_jump_start", ("jumpstart", "jump_start", "jump start", "jumpup")),
    ("anim_in_air", ("inair", "in_air", "falling", "fall", "airborne", "jump")),
    ("anim_slide", ("slide",)),
    ("anim_climb_loop", ("climb",)),
    ("anim_weapon_fire", ("shoot", "fire", "firing", "attack", "throw")),
    ("anim_reload", ("reload",)),
    ("anim_melee_swing", ("swing", "slash", "melee", "punch", "stab")),
    ("anim_ability", ("cast", "ability", "dash", "skill", "spell")),
    ("anim_walk", ("walk",)),
    ("anim_run", ("run", "sprint", "jog")),
    ("anim_idle", ("idle",)),
]

HEADER = 84


def die(msg, code=1):
    print("mixamo_clipset: " + msg, file=sys.stderr)
    return code


def read_binary(path):
    """Return (header dict, data bytes) for a .gband, or raise ValueError with the real reason."""
    with open(path, "rb") as f:
        raw = f.read()
    if len(raw) < HEADER:
        raise ValueError("%s: shorter than the 84-byte header" % path)
    magic, version, tick_rate, duration, channels = struct.unpack_from("<4sIIII", raw, 0)
    if magic != b"GBND":
        raise ValueError("%s: not a .gband (magic %r)" % (path, magic))
    if version != 1:
        raise ValueError("%s: unsupported gband version %d" % (path, version))
    if duration == 0 or channels == 0:
        raise ValueError("%s: duration_ticks or num_channels is zero" % path)
    expected = HEADER + duration * channels * 4
    if len(raw) != expected:
        raise ValueError("%s: %d bytes, header says %d (%d ticks x %d channels)"
                         % (path, len(raw), expected, duration, channels))
    data = raw[HEADER:]
    return {
        "version": version, "tick_rate": tick_rate, "duration_ticks": duration,
        "num_channels": channels,
        "skeleton_hash": raw[20:52].hex(),
        "content_hash": raw[52:84].hex(),
    }, data


def load_manifest(path):
    with open(path, "r") as f:
        return json.load(f)


def validate_pair(stem, assets):
    """Validate one .gband/.gband.json pair. Returns (header, manifest). Raises ValueError."""
    gb = os.path.join(assets, stem + ".gband")
    mj = os.path.join(assets, stem + ".gband.json")
    if not os.path.isfile(gb):
        raise ValueError("%s: no %s.gband" % (stem, stem))
    if not os.path.isfile(mj):
        raise ValueError("%s: no %s.gband.json (channels live there; the binary is channel-blind)" % (stem, stem))
    header, data = read_binary(gb)
    man = load_manifest(mj)

    if int(man.get("duration_ticks", -1)) != header["duration_ticks"]:
        raise ValueError("%s: manifest duration %r != binary %d" % (stem, man.get("duration_ticks"), header["duration_ticks"]))
    if int(man.get("tick_rate", -1)) != header["tick_rate"]:
        raise ValueError("%s: manifest tick_rate %r != binary %d" % (stem, man.get("tick_rate"), header["tick_rate"]))
    chans = man.get("channels")
    if not isinstance(chans, list) or len(chans) != header["num_channels"]:
        raise ValueError("%s: manifest has %s channels, binary has %d (their ORDER is the data layout)"
                         % (stem, len(chans) if isinstance(chans, list) else "no", header["num_channels"]))
    for c in chans:
        if not isinstance(c, str) or "." not in c:
            raise ValueError("%s: channel %r is not '<joint>.<tx|qy|...>'" % (stem, c))
    got = hashlib.sha256(data).hexdigest()
    if got != header["content_hash"]:
        raise ValueError("%s: content_hash mismatch (file says %s, data hashes to %s) -- re-export or rehash"
                         % (stem, header["content_hash"][:12], got[:12]))
    declared = man.get("content_hash")
    if declared and declared.lower() != header["content_hash"]:
        raise ValueError("%s: manifest content_hash %s != binary %s" % (stem, declared[:12], header["content_hash"][:12]))
    if man.get("skeleton_hash") and man["skeleton_hash"].lower() != header["skeleton_hash"]:
        raise ValueError("%s: manifest skeleton_hash != binary's" % stem)
    return header, man


def auto_map(stems, rig=None):
    """Propose a mapping from clip names. Ambiguity is an error, never a coin flip.

    Greedy in AUTO_RULES order (specific before generic) and each stem is claimed once: without
    that, "mixamo_jump_start" matches both JUMP_START and IN_AIR's own "jump" keyword and every run
    of --auto would be ambiguous for a completely normal clip library.
    """
    pool = [s for s in stems if not rig or rig.lower() in s.lower()]
    found, missing, ambiguous = {}, [], []
    claimed = set()
    for state, keys in AUTO_RULES:
        hits = []
        for stem in pool:
            if stem in claimed:
                continue
            low = stem.lower().replace("-", "_")
            if any(k in low for k in keys):
                hits.append(stem)
        if not hits:
            missing.append(state)
        elif len(hits) > 1:
            # Ties are only acceptable when one stem is an exact key match.
            exact = [h for h in hits if h.lower() in keys]
            if len(exact) == 1:
                found[state] = exact[0]
                claimed.add(exact[0])
            else:
                ambiguous.append((state, sorted(hits)))
        else:
            found[state] = hits[0]
            claimed.add(hits[0])
    return found, missing, ambiguous


def write_bundle(mapping, assets, out, allow_partial=False, mesh=None):
    """Validate the whole mapping, then write <out>/<contract>.gband(+.json). Returns warnings.

    `mesh` names the .gskel/.gmesh in `assets` these clips were made against, and is recorded as
    <out>/skin.json. Without it the bundle is still a valid clip set, but nothing can DRAW it: a
    clip is channel data and channels are only meaningful against one skeleton, so the engine needs
    to be told which skin to put them on.
    """
    errors, warnings, headers, manifests = [], [], {}, {}
    used = {}
    for state, stem in mapping.items():
        if state not in CONTRACT:
            errors.append("%s: not a contract name" % state)
            continue
        if stem in used:
            warnings.append("%s and %s share the clip %s (allowed, but two states will look identical)"
                            % (used[stem], state, stem))
        used[stem] = state
        try:
            header, man = validate_pair(stem, assets)
        except (ValueError, OSError) as e:
            errors.append(str(e))
            continue
        headers[state] = header
        manifests[state] = man

    missing = [s for s in CONTRACT if s not in mapping]
    if missing and not allow_partial:
        errors.append("missing contract clips: " + ", ".join(missing))

    skels = {}
    for state, h in headers.items():
        skels.setdefault(h["skeleton_hash"], []).append(state)
    if len(skels) > 1:
        detail = "; ".join("%s <- %s" % (k[:10], ", ".join(v)) for k, v in skels.items())
        errors.append("clips come from %d different skeletons (%s) -- one bundle is one rig" % (len(skels), detail))

    if errors:
        return errors, warnings

    os.makedirs(out, exist_ok=True)
    for state in CONTRACT:
        if state not in mapping:
            continue
        stem = mapping[state]
        man = dict(manifests[state])
        h = headers[state]
        loops = state in LOOPING
        # Keep every field that describes the DATA (channels order, hashes, rates) and own only the
        # fields that describe the CONTRACT (name, intent, loop window). Authorship is preserved.
        man["gband_version"] = 1
        man["tick_rate"] = h["tick_rate"]
        man["duration_ticks"] = h["duration_ticks"]
        man["skeleton_hash"] = h["skeleton_hash"]
        man["content_hash"] = h["content_hash"]
        man["contract_state"] = state
        man["contract_index"] = CONTRACT.index(state)
        man["source_clip"] = stem
        man["loop"] = bool(loops)
        man["loop_points"] = {"start_tick": 0, "end_tick": h["duration_ticks"] if loops else 0}
        tags = [t for t in man.get("intent_tags", []) if isinstance(t, str)]
        if loops and "gait" not in tags and state in ("anim_walk", "anim_run"):
            tags.append("gait")
        man["intent_tags"] = tags
        shutil.copyfile(os.path.join(assets, stem + ".gband"), os.path.join(out, state + ".gband"))
        with open(os.path.join(out, state + ".gband.json"), "w") as f:
            json.dump(man, f, indent=2, sort_keys=True)
            f.write("\n")

    if mesh:
        skin = {
            "mesh": mesh,
            "source": "mixamo_clipset.py",
            "assets": os.path.basename(os.path.abspath(assets)),
            "contract_version": 1,
            "states": [s for s in CONTRACT if s in mapping],
        }
        with open(os.path.join(out, "skin.json"), "w") as f:
            json.dump(skin, f, indent=2, sort_keys=True)
            f.write("\n")
    return [], warnings


def main(argv):
    ap = argparse.ArgumentParser(description="bind Mixamo/GOLDENBAND clips to the 13-name animation contract")
    ap.add_argument("--assets", default="assets/goldenband", help="directory holding <stem>.gband + .gband.json")
    ap.add_argument("--map", help="JSON object: contract name -> clip stem")
    ap.add_argument("--auto", action="store_true", help="propose the mapping from clip names")
    ap.add_argument("--rig", help="with --auto: only consider clips whose name contains this")
    ap.add_argument("--out", required=True, help="output directory for the bundle")
    ap.add_argument("--allow-partial", action="store_true", help="write the states that are mapped (still validates each)")
    ap.add_argument("--mesh", help="the .gskel/.gmesh in --assets these clips belong to (written to skin.json)")
    ap.add_argument("--dry-run", action="store_true", help="validate and report, write nothing")
    args = ap.parse_args(argv)

    if not os.path.isdir(args.assets):
        return die("assets directory not found: %s" % args.assets, 2)

    if args.map:
        try:
            with open(args.map, "r") as f:
                mapping = json.load(f)
        except (OSError, ValueError) as e:
            return die("cannot read --map: %s" % e, 2)
        if not isinstance(mapping, dict):
            return die("--map must be a JSON object of contract name -> clip stem", 2)
    elif args.auto:
        stems = sorted({f[:-len(".gband")] for f in os.listdir(args.assets) if f.endswith(".gband")})
        found, missing, ambiguous = auto_map(stems, args.rig)
        if ambiguous:
            for state, hits in ambiguous:
                print("mixamo_clipset: %s is ambiguous: %s" % (state, ", ".join(hits)), file=sys.stderr)
            print("mixamo_clipset: name the winner explicitly with --map", file=sys.stderr)
            return 2
        if missing:
            print("mixamo_clipset: no clip found for: %s" % ", ".join(missing), file=sys.stderr)
            if not args.allow_partial:
                return 2
        mapping = found
    else:
        return die("one of --map or --auto is required", 2)

    if args.dry_run:
        errors, warnings = [], []
        for state, stem in sorted(mapping.items()):
            try:
                validate_pair(stem, args.assets)
            except (ValueError, OSError) as e:
                errors.append(str(e))
        for state in CONTRACT:
            if state not in mapping:
                errors.append("missing contract clip: " + state)
    else:
        errors, warnings = write_bundle(mapping, args.assets, args.out, args.allow_partial, args.mesh)

    for w in warnings:
        print("warn: " + w, file=sys.stderr)
    if errors:
        for e in errors:
            print("error: " + e, file=sys.stderr)
        return 1
    print("ok: %d/%d clips bound%s -> %s" % (len(mapping), len(CONTRACT),
                                            " (dry run)" if args.dry_run else "", args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
