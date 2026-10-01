#!/usr/bin/env python3
"""brick_e2e.py -- end-to-end test of destructible brick over the REAL network path.

Starts a real bin/shank_server on a private UDP port with a brick level, connects a real packet
client (the same wire protocol the game and the RL bots speak), shoots a single-cell brick crate with
a magnum, and checks that the server (the only authority) destroyed the cell and told the client via
PACKET_BRICK_STATE; then connects a second, LATE client and checks it learns the same fact from the
server's background refresh with no help from the shooter.

    make server && python3 scripts/brick_e2e.py [--port 6995]

Never touches the live servers (private port, own temp level file, exact-PID cleanup only).
"""
import argparse, json, os, socket, struct, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(__file__))
from rl_env_packet import PacketClient, encode_usercmd, UserCmd  # noqa: E402

PACKET_BRICK_STATE = 13
BTN_ATTACK = 2
WPN_MAGNUM = 1
HDR = 12          # sizeof(NetHeader)
ENTRY = 8         # sizeof(NetBrickEntry)


def level_json():
    def wall(i, x, y, z, sx, sy, sz, mat):
        return {"id": i, "x": x, "y": y, "z": z, "sx": sx, "sy": sy, "sz": sz,
                "r": 0.6, "g": 0.4, "b": 0.3, "friction": 0, "material": mat}
    walls = [
        wall(1, 0, -2, 0, 140, 4, 140, "concrete"),
        wall(2, 0, 8, -30, 50, 16, 4, "brick"),
        wall(3, -40, 8, 10, 6, 16, 30, "brick"),
        wall(4, 40, 12, 0, 16, 24, 16, "brick"),
        wall(5, -8, 2, 22, 4, 4, 4, "brick"),
        wall(6, 0, 2, 22, 4, 4, 4, "brick"),     # box index 5: the one we shoot
        wall(7, 8, 2, 22, 4, 4, 4, "brick"),
        wall(8, 0, 8, 0, 4, 16, 4, "metal"),
    ]
    return {"version": 1, "name": "BRICK_E2E", "width": 140, "height": 30, "depth": 140,
            "ground_plane_enabled": True, "ground_plane_squares": 24, "walls": walls,
            "spawners": [{"id": 1, "x": 0, "y": 1, "z": 50, "yaw": 0, "team": -1}]}


def parse_brick(data):
    """-> list of (parent, key, hp) or None if not a brick packet / malformed."""
    if len(data) < HDR + 4 or data[0] != PACKET_BRICK_STATE:
        return None
    count = data[HDR]
    out = []
    for i in range(count):
        off = HDR + 4 + i * ENTRY
        if off + ENTRY > len(data):
            break
        parent, klo, khi, hp, _ = struct.unpack_from("<HHHBB", data, off)
        out.append((parent, klo | (khi << 16), hp))
    return out


def drain(client, seconds, state, fire=False, seq=[0]):
    """Spend `seconds` talking to the server; returns the brick-state entries seen."""
    end = time.time() + seconds
    client.sock.settimeout(0.01)
    while time.time() < end:
        if fire is not None:
            seq[0] += 1
            cmd = UserCmd(sequence=seq[0], timestamp=int(time.time() * 1000) & 0xFFFFFFFF, msec=16,
                          fwd=0.0, str_=0.0, yaw=0.0, pitch=0.0,
                          buttons=BTN_ATTACK if fire else 0, weapon_idx=WPN_MAGNUM)
            client.sock.sendto(encode_usercmd(cmd), (client.host, client.port))
        try:
            while True:
                data, _ = client.sock.recvfrom(65536)
                got = parse_brick(data)
                if got:
                    state["packets"] += 1
                    for (p, k, hp) in got:
                        state["cells"][(p, k)] = hp
        except (socket.timeout, BlockingIOError):
            pass
        time.sleep(0.016)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=6995)
    ap.add_argument("--server", default=os.path.join(os.path.dirname(__file__), "..", "bin", "shank_server"))
    a = ap.parse_args()
    if a.port in (6969, 6970, 6971, 6978):
        sys.exit("refusing to use a live-service port")

    tmp = tempfile.mkdtemp(prefix="brick_e2e_")
    lvl = os.path.join(tmp, "level.json")
    json.dump(level_json(), open(lvl, "w"))
    log = open(os.path.join(tmp, "server.log"), "w")
    srv = subprocess.Popen([a.server, "--deathmatch", "--port", str(a.port), "--level", lvl],
                           stdout=log, stderr=subprocess.STDOUT, cwd=tmp)
    ok = True
    try:
        time.sleep(1.5)
        if srv.poll() is not None:
            print("FAIL: server exited early; see", log.name)
            return 1
        shooter = PacketClient("127.0.0.1", a.port)
        assert shooter.connect(game_mode=0), "shooter could not connect"
        seen_a = {"packets": 0, "cells": {}}
        drain(shooter, 1.0, seen_a, fire=False)                     # settle, no shooting
        drain(shooter, 7.0, seen_a, fire=True)                      # magnum at the middle crate
        drain(shooter, 1.0, seen_a, fire=False)
        crate = (5, 0)
        print("shooter saw", seen_a["packets"], "brick packets; crate cell hp =", seen_a["cells"].get(crate))
        if seen_a["cells"].get(crate) != 0:
            print("FAIL: the crate cell was not reported destroyed to the shooter"); ok = False
        # authority: nothing outside the crate's neighbourhood may be damaged by shooting a crate
        stray = {c: hp for c, hp in seen_a["cells"].items() if c[0] not in (5, 4, 6, 7)}
        if stray:
            print("note: other cells touched:", stray)

        late = PacketClient("127.0.0.1", a.port)
        assert late.connect(game_mode=0), "late client could not connect"
        seen_b = {"packets": 0, "cells": {}}
        drain(late, 3.0, seen_b, fire=None)                         # listen only, never shoots
        print("late joiner saw", seen_b["packets"], "brick packets; crate cell hp =", seen_b["cells"].get(crate))
        if seen_b["cells"].get(crate) != 0:
            print("FAIL: the late joiner never learned the crate is gone"); ok = False
    finally:
        srv.terminate()
        try:
            srv.wait(timeout=5)
        except subprocess.TimeoutExpired:
            srv.kill()
        log.close()
    print("brick_e2e:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
