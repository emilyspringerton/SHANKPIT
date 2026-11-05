#!/usr/bin/env python3
"""vendor_sale_e2e.py -- the REAL server, a REAL UDP client, and an ice-cream CONE observed on the wire.

The named limit this closes: "the cone over a live two-client wire is still unobserved -- a connecting client
spawns at 100/100, so a 25-point heal is invisible in the snapshot". Two named test knobs (off by default,
the same convention as SHANKPIT_TEST_ZOMBIE_PAIR) make that observable without touching a normal run:

  SHANKPIT_TEST_START_HEALTH=N   the buyer connects wounded
  SHANKPIT_TEST_VENDOR_AT=dx,dz  the vendor stands dx,dz from the buyer, brakes held

Then this script asserts what actually ARRIVES at the client:

  1. the buyer's own row in PACKET_SNAPSHOT reads the health the server was told to start at;
  2. after PARENA's icecream-serve-ms of standing there, that same row reads exactly icecream-serve-heal MORE
     -- the server healed the authoritative PlayerState and the snapshot carried it, which is the whole claim;
  3. it stays there through the cooldown (no double cone, no client-side second heal);
  4. no row ever reads above 100.

No number here is restated: a tiny C probe prints the struct sizes/offsets AND the heal and hold values from
the real headers, so editing the rules changes the test rather than breaking it.
"""
import os, socket, struct, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVER = os.path.join(ROOT, "bin", "shank_server")
PORT = 6978
PACKET_CONNECT = 0
PACKET_USERCMD = 1
PACKET_SNAPSHOT = 2
START_HEALTH = 40
VENDOR_AT = "2,0"


def probe_layout():
    """Sizes, offsets and the two rule values, straight from the real headers."""
    src = r'''
#include <stdio.h>
#include <stddef.h>
#include "protocol.h"
#include "icecream_rules.h"
int main(void) {
    printf("%zu %zu %zu %zu %zu %d %zu %zu %d %d %d\n",
           sizeof(NetHeader), 1 + sizeof(NetHeader), sizeof(NetPlayer),
           offsetof(NetPlayer, id), offsetof(NetPlayer, health), MAX_CLIENTS,
           sizeof(UserCmd), offsetof(UserCmd, sequence),
           icecream_serve_heal(), icecream_serve_ms(), icecream_window_units());
    return 0;
}
'''
    with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as f:
        f.write(src)
        path = f.name
    out = tempfile.mktemp(suffix=".bin")
    subprocess.check_call(["gcc", "-std=gnu99", "-I" + os.path.join(ROOT, "packages/common"),
                           "-I" + os.path.join(ROOT, "packages/simulation"),
                           path, os.path.join(ROOT, "packages/simulation/icecream_rules.c"),
                           "-lm", "-o", out])
    text = subprocess.check_output([out]).decode().split()
    os.unlink(path)
    os.unlink(out)
    v = [int(x) for x in text[:11]]
    return dict(hdr=v[0], players_off=v[1], np=v[2], id_off=v[3], health_off=v[4], max_clients=v[5],
                uc=v[6], seq_off=v[7], heal=v[8], hold_ms=v[9], window=v[10])


def snapshots(data, lay):
    """Every (player id, health) row in a PACKET_SNAPSHOT. Layout: header, 1 count byte, count * NetPlayer."""
    if not data or data[0] != PACKET_SNAPSHOT:
        return []
    if len(data) < lay["players_off"]:
        return []
    rows = []
    count = data[lay["hdr"]]
    for i in range(count):
        base = lay["players_off"] + i * lay["np"]
        if base + lay["np"] > len(data):
            break
        rows.append((data[base + lay["id_off"]], data[base + lay["health_off"]]))
    return rows


def main():
    if not os.path.exists(SERVER):
        print("SKIP: bin/shank_server not built")
        return 2
    lay = probe_layout()
    expect = START_HEALTH + lay["heal"]
    print("vendor_sale_e2e: window %d units, hold %d ms, heal %d, start %d -> expect %d"
          % (lay["window"], lay["hold_ms"], lay["heal"], START_HEALTH, expect))

    env = dict(os.environ)
    env["SHANKPIT_TEST_START_HEALTH"] = str(START_HEALTH)
    env["SHANKPIT_TEST_VENDOR_AT"] = VENDOR_AT
    log = tempfile.NamedTemporaryFile("w", suffix=".log", delete=False)
    srv = subprocess.Popen([SERVER, "--port", str(PORT), "--beacon-port", "0"],
                           stdout=log, stderr=subprocess.STDOUT, cwd=ROOT, env=env)
    try:
        time.sleep(1.0)
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(0.05)
        addr = ("127.0.0.1", PORT)

        hdr = bytearray(lay["hdr"])
        hdr[0] = PACKET_CONNECT
        sock.sendto(bytes(hdr), addr)
        time.sleep(0.3)

        checks = 0

        def ok(cond, msg):
            nonlocal checks
            checks += 1
            if not cond:
                raise AssertionError(msg)

        seq = 0
        mine = None
        first_seen = None
        sold_at = None
        last = {}
        max_seen = 0
        deadline = time.time() + 25.0
        while time.time() < deadline:
            # a scripted client has no keyboard: the USERCMD is what makes the server treat it as a live player
            seq += 1
            cmd = bytearray(lay["hdr"] + lay["uc"])
            cmd[0] = PACKET_USERCMD
            struct.pack_into("<I", cmd, lay["hdr"] + lay["seq_off"], seq)
            sock.sendto(bytes(cmd), addr)
            try:
                data, _ = sock.recvfrom(4096)
            except socket.timeout:
                continue
            now = time.time()
            for pid, hp in snapshots(data, lay):
                last[pid] = hp
                if hp > max_seen:
                    max_seen = hp
                if mine is None and hp == START_HEALTH:
                    mine = pid
                    first_seen = now
                elif mine is not None and pid == mine and hp == expect and sold_at is None:
                    sold_at = now
            if sold_at is not None and time.time() - sold_at > 2.5:
                break

        ok(mine is not None,
           "no player row ever read the %d health the server was told to start at (rows: %s)"
           % (START_HEALTH, sorted(last.items())))
        ok(sold_at is not None,
           "the buyer's row stayed at %d -- no cone ever reached the snapshot (rows: %s)"
           % (last.get(mine), sorted(last.items())))
        waited = sold_at - first_seen
        print("      buyer is player %d: %d -> %d after %.0f ms on the wire"
              % (mine, START_HEALTH, expect, waited * 1000.0))
        ok(waited * 1000.0 >= lay["hold_ms"] * 0.5,
           "the cone arrived %.0f ms after the buyer was visible, well inside the %d ms hold"
           % (waited * 1000.0, lay["hold_ms"]))
        ok(last[mine] == expect,
           "the buyer's health drifted to %d after the cone (expected %d)" % (last[mine], expect))
        ok(max_seen <= 100, "some player row read %d health" % max_seen)
        print("      still %d two and a half seconds later (the cooldown held), no row above %d"
              % (last[mine], max_seen))

        print("vendor_sale_e2e: %d checks passed" % checks)
        return 0
    except AssertionError as e:
        print("FAIL: %s" % e)
        return 1
    finally:
        srv.terminate()
        try:
            srv.wait(timeout=5)
        except subprocess.TimeoutExpired:
            srv.kill()
        log.close()
        text = open(log.name).read()
        if os.environ.get("SHOW_SERVER_LOG") or os.environ.get("_VENDOR_E2E_LOG"):
            print("--- server log (tail) ---")
            print(text[-3000:])
        else:
            for line in text.splitlines():
                if "VENDOR" in line or "TEST_" in line:
                    print("      server: " + line)


if __name__ == "__main__":
    sys.exit(main())
