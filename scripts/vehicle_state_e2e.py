#!/usr/bin/env python3
"""vehicle_state_e2e.py -- the REAL server, a REAL UDP client, and PACKET_VEHICLE_STATE.

There was no end-to-end test for the vehicle packet: it was compiled and unit-tested, never observed on a
wire. This starts bin/shank_server, connects the way a client does, calls a buggy into the world through
PACKET_KIT_CALL, and then asserts what actually arrives:

  1. a PACKET_VEHICLE_STATE (type 20) shows up at all, for a car we caused;
  2. its entries are self-consistent -- mask bits within the car's components, hp within the part's max,
     a part marked off holding 0 hull points;
  3. it is ABSOLUTE and idempotent: a second packet agrees with the first unless the car took damage, so
     applying it twice, late, or out of order cannot desync (protocol.h's own contract).

Sizes and offsets are not guessed: a tiny C probe prints them from the real headers.
"""
import os, socket, struct, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVER = os.path.join(ROOT, "bin", "shank_server")
PORT = 6977
PACKET_CONNECT = 0
PACKET_KIT_CALL = 19
PACKET_VEHICLE_STATE = 20
KIT_ITEM_BUGGY = 5
MAX_COMPONENTS = 5
SALVAGE_HULL = 300


def probe_layout():
    """sizeof/offsetof straight from the real protocol.h -- never a hand-copied number."""
    src = r'''
#include <stdio.h>
#include <stddef.h>
#include "protocol.h"
int main(void) {
    printf("%zu %zu %zu %zu %zu %zu\n",
           sizeof(NetHeader), offsetof(NetVehicleState, count),
           offsetof(NetVehicleState, e), sizeof(NetVehicleEntry),
           offsetof(NetVehicleEntry, hp), sizeof(NetVehicleState));
    return 0;
}
'''
    with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as f:
        f.write(src)
        path = f.name
    out = tempfile.mktemp(suffix=".bin")
    subprocess.check_call(["gcc", "-std=gnu99", "-I" + os.path.join(ROOT, "packages/common"),
                           path, "-o", out])
    text = subprocess.check_output([out]).decode().split()
    os.unlink(path); os.unlink(out)
    vals = [int(v) for v in text[:6]]
    return dict(hdr=vals[0], count_off=vals[1], entries_off=vals[2], entry=vals[3],
                hp_off=vals[4], whole=vals[5])


def main():
    if not os.path.exists(SERVER):
        print("SKIP: bin/shank_server not built"); return 2
    lay = probe_layout()
    log = tempfile.NamedTemporaryFile("w", suffix=".log", delete=False)
    srv = subprocess.Popen([SERVER, "--port", str(PORT), "--beacon-port", "0"],
                           stdout=log, stderr=subprocess.STDOUT, cwd=ROOT)
    try:
        time.sleep(1.0)
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(0.25)
        addr = ("127.0.0.1", PORT)

        # --- connect exactly as a client does: a bare header ---
        hdr = bytearray(lay["hdr"])
        hdr[0] = PACKET_CONNECT
        sock.sendto(bytes(hdr), addr)
        time.sleep(0.6)

        # --- ask the SERVER to put a car in the world ---
        call = bytearray(lay["hdr"] + 4)
        call[0] = PACKET_KIT_CALL
        call[lay["hdr"]] = KIT_ITEM_BUGGY
        sock.sendto(bytes(call), addr)

        packets = []
        deadline = time.time() + 7.0
        reask_at = time.time() + 0.5
        while time.time() < deadline:
            # a call is refused before the slot is welcomed and again while the car cooldown runs, so
            # re-assert the intent for a few seconds rather than betting one datagram on the timing
            if time.time() >= reask_at and time.time() < deadline - 3.0:
                sock.sendto(bytes(call), addr)
                reask_at = time.time() + 0.5
            try:
                data, _ = sock.recvfrom(4096)
            except socket.timeout:
                continue
            if not data:
                continue
            packets.append(data)
            if sum(1 for p in packets if p[0] == PACKET_VEHICLE_STATE) >= 2:
                break

        veh = [p for p in packets if len(p) > lay["hdr"] and p[0] == PACKET_VEHICLE_STATE]
        if not veh:
            seen = sorted({p[0] for p in packets})
            print("FAIL: no PACKET_VEHICLE_STATE arrived (types seen: %s)" % seen)
            return 1

        checks = 0
        def ok(cond, msg):
            nonlocal checks
            checks += 1
            if not cond:
                raise AssertionError(msg)

        for pkt in veh:
            count = pkt[lay["count_off"]]
            ok(count >= 1, "packet carries %d cars" % count)
            for i in range(count):
                base = lay["entries_off"] + i * lay["entry"]
                if base + lay["entry"] > len(pkt):
                    break
                cid, kind, alive, detached = pkt[base], pkt[base+1], pkt[base+2], pkt[base+3]
                ok(cid < 16, "car slot %d is not a buggy slot" % cid)
                ok(kind in (0, 1), "car %d kind %d" % (cid, kind))
                valid = 0x0F if kind == 0 else 0x1F
                ok(alive & ~valid == 0, "car %d alive mask 0x%02x exceeds its %s parts" % (cid, alive, "4" if kind == 0 else "5"))
                ok(detached & ~valid == 0, "car %d detached mask 0x%02x exceeds its parts" % (cid, detached))
                hp = list(pkt[base+lay["hp_off"]-base+0: base+lay["hp_off"]-base+MAX_COMPONENTS]) if False else \
                     list(pkt[base + (lay["hp_off"] - 0): base + (lay["hp_off"] - 0) + MAX_COMPONENTS])
                total = 0
                for c in range(MAX_COMPONENTS):
                    if c >= (4 if kind == 0 else 5):
                        ok(hp[c] == 0, "car %d part %d exists but the kind has only %d" % (cid, c, 4 if kind == 0 else 5))
                        continue
                    if not (alive >> c) & 1:
                        ok(hp[c] == 0, "car %d part %d is OFF and still claims %d hull" % (cid, c, hp[c]))
                    total += hp[c]
                ok(0 <= total <= SALVAGE_HULL, "car %d parts sum to %d, hull is %d" % (cid, total, SALVAGE_HULL))

        # --- absolute and monotonic: a car described twice keeps its slot and kind, and its hull points
        # never go UP (damage only ever removes), which is what makes a late or reordered packet safe ---
        if len(veh) >= 2:
            a, b = veh[0], veh[1]
            common = 0
            for i in range(min(a[lay["count_off"]], b[lay["count_off"]])):
                ba = lay["entries_off"] + i * lay["entry"]
                if ba + lay["entry"] > len(b):
                    break
                ok(a[ba] == b[ba], "the same index describes two different cars (%d vs %d)" % (a[ba], b[ba]))
                ok(a[ba + 1] == b[ba + 1], "car %d changed kind between packets" % a[ba])
                for c in range(MAX_COMPONENTS):
                    pa = a[ba + lay["hp_off"] + c]
                    pb = b[ba + lay["hp_off"] + c]
                    ok(pb <= pa, "car %d part %d grew %d -> %d between packets" % (a[ba], c, pa, pb))
                common += 1
            ok(common >= 1, "the two packets shared no car to compare")

        print("vehicle_state_e2e: %d checks passed, %d packet(s), first count=%d"
              % (checks, len(veh), veh[0][lay["count_off"]]))
        return 0
    finally:
        srv.terminate()
        try:
            srv.wait(timeout=5)
        except subprocess.TimeoutExpired:
            srv.kill()
        log.close()
        if os.environ.get("SHOW_SERVER_LOG"):
            print("--- server log ---")
            print(open(log.name).read()[-3000:])


if __name__ == "__main__":
    sys.exit(main())
