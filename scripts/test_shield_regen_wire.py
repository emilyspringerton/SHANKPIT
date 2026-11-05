#!/usr/bin/env python3
"""test_shield_regen_wire.py -- the shield regen clock through a REAL server over REAL UDP, with a
delaying proxy in the path.

Every earlier regen test either mocked the wire (the host suite) or decoded a hand-built buffer (the
Go/Python decode tests). This one starts the real `bin/shank_server`, puts a userspace UDP proxy in
front of it that holds every downstream datagram for a fixed delay plus jitter, connects as a scripted
client through that proxy, and watches a real broken shield come back:

  1. the wire's `shield_regen_since_ms` is ELAPSED time, so it advances ~1:1 with the wall clock even
     though every packet arrives DELAY_MS late (a deadline would show up as a constant offset instead);
  2. the ramp lands on the wire -- the shield takes +1, then +2, then +3 (1, 3, 6 points);
  3. the proxy really did delay the traffic, so the test is not passing on an empty path.

The server's SHANKPIT_TEST_START_SHIELD=N knob breaks the connecting client's shield at CONNECT (a
fresh spawn is 100/100, so the window would never be observable otherwise), starting the clock at that
exact instant.

    make test-shield-regen-wire
"""

import os
import random
import select
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rl_env_packet as rl  # noqa: E402

DELAY_MS = 120
JITTER_MS = 40
RUN_S = 9.0


def free_udp_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class DelayProxy:
    """client <--[delay+jitter]-- proxy <--immediate-- server.

    Upstream (client -> server) is forwarded at once, because a delayed input would just be a laggy
    player; downstream is held, which is what a congested path does to the snapshots a client reads.
    """

    def __init__(self, listen_port, server_addr):
        self.client_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.client_sock.bind(("127.0.0.1", listen_port))
        self.client_sock.setblocking(False)
        self.up_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.up_sock.bind(("127.0.0.1", 0))
        self.up_sock.setblocking(False)
        self.server_addr = server_addr
        self.client_addr = None
        self.pending = []          # (deliver_at, data)
        self.holds_ms = []
        self.forwarded = 0
        self._stop = False

    def run(self):
        while not self._stop:
            now = time.time()
            keep = []
            for at, data in self.pending:
                if at <= now and self.client_addr:
                    self.client_sock.sendto(data, self.client_addr)
                    self.forwarded += 1
                else:
                    keep.append((at, data))
            self.pending = keep
            ready, _, _ = select.select([self.client_sock, self.up_sock], [], [], 0.02)
            for s in ready:
                try:
                    data, addr = s.recvfrom(65536)
                except (BlockingIOError, socket.error):
                    continue
                if s is self.client_sock:
                    self.client_addr = addr
                    self.up_sock.sendto(data, self.server_addr)
                else:
                    hold = (DELAY_MS + random.random() * JITTER_MS) / 1000.0
                    self.holds_ms.append(hold * 1000.0)
                    self.pending.append((time.time() + hold, data))

    def stop(self):
        self._stop = True


def main():
    port, proxy_port = free_udp_port(), free_udp_port()
    log = tempfile.NamedTemporaryFile(prefix="shank_regen_wire_", suffix=".log", delete=False)
    env = dict(os.environ)
    env["SHANKPIT_TEST_START_SHIELD"] = "0"
    env["SHANKPIT_BAKED_LEVELS"] = "1"     # no level download in a wire test

    srv = subprocess.Popen(["./bin/shank_server", "--port", str(port), "--deathmatch"],
                           env=env, stdout=log, stderr=subprocess.STDOUT)
    proxy = DelayProxy(proxy_port, ("127.0.0.1", port))
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(1.0)
    failures = []

    def check(cond, what):
        print(("  ok   " if cond else "  FAIL ") + what)
        if not cond:
            failures.append(what)

    try:
        time.sleep(1.5)
        if srv.poll() is not None:
            with open(log.name) as f:
                print(f.read()[-2000:])
            print("FAIL: server exited before the test could connect")
            return 1
        import threading
        threading.Thread(target=proxy.run, daemon=True).start()

        sock.sendto(rl.encode_connect(), ("127.0.0.1", proxy_port))
        cid = None
        deadline = time.time() + 6.0
        while time.time() < deadline and cid is None:
            try:
                data, _ = sock.recvfrom(4096)
            except socket.timeout:
                continue
            if data and data[0] == rl.PACKET_WELCOME:
                welcome = rl.decode_welcome(data)
                if welcome:
                    cid = welcome[0]
        check(cid is not None, "a scripted client completed the handshake THROUGH the delaying proxy")
        if cid is None:
            return 1
        print(f"  client_id={cid}, proxy delay {DELAY_MS}+0..{JITTER_MS} ms")

        samples = []      # (seconds since start, shield_regen_since_ms, shield)
        seq = 0
        start = time.time()
        while time.time() - start < RUN_S:
            seq += 1
            cmd = rl.UserCmd(sequence=seq, timestamp=int(time.time() * 1000) & 0xFFFFFFFF, msec=16,
                             fwd=0.0, str_=0.0, yaw=0.0, pitch=0.0, buttons=0, weapon_idx=rl.WPN_MAGNUM)
            sock.sendto(rl.encode_usercmd(cmd), ("127.0.0.1", proxy_port))
            try:
                data, _ = sock.recvfrom(65536)
            except socket.timeout:
                continue
            if not data or data[0] != rl.PACKET_SNAPSHOT:
                continue
            ents = rl.decode_snapshot(data)
            if not ents:
                continue
            me = next((e for e in ents if e.id == cid), None)
            if me is not None:
                samples.append((time.time() - start, me.shield_regen_since_ms, me.shield))
            time.sleep(0.02)

        check(len(samples) >= 40, f"the wire delivered real snapshots ({len(samples)} samples)")
        if not samples:
            return 1

        # 1. elapsed, not a deadline: the clock moves with the wall clock, delayed arrival and all.
        clocked = [(t, s) for (t, s, _) in samples if s > 0]
        check(len(clocked) >= 20, f"the broken shield's clock is on the wire ({len(clocked)} samples with since > 0)")
        slope = None
        if len(clocked) >= 2:
            dt = clocked[-1][0] - clocked[0][0]
            ds = clocked[-1][1] - clocked[0][1]
            # the clock is in MILLISECONDS and the sample times in seconds
            slope = ds / (dt * 1000.0) if dt > 0 else None
            print(f"  clock slope: {slope:.3f} ms per ms over {dt:.2f} s of arrivals")
            check(slope is not None and 0.75 <= slope <= 1.30,
                  "the wire clock advances ~1:1 with wall time despite the delay")
        check(all(b[1] >= a[1] for a, b in zip(samples, samples[1:])),
              "the wire clock never runs backwards")

        # 2. the ramp, on the wire: 1 point, then +2 (3), then +3 (6).
        shields = [sh for (_, _, sh) in samples]
        seen = set(shields)
        check(1 in seen, "the first tick (+1) was observed on the wire")
        check(3 in seen, "the second tick (+2, total 3) was observed on the wire")
        check(6 in seen, "the third tick (+3, total 6) was observed on the wire")
        check(0 in seen, "the four-second window really was spent at zero first")
        check(max(shields) < 25, f"the ramp is still climbing, not filling the bar ({max(shields)} points)")

        # 3. the path was actually delayed, so the rest is not a loopback accident.
        mean_hold = sum(proxy.holds_ms) / len(proxy.holds_ms) if proxy.holds_ms else 0.0
        print(f"  proxy held {len(proxy.holds_ms)} downstream datagrams, mean {mean_hold:.1f} ms")
        check(len(proxy.holds_ms) >= 40, "the proxy delayed a real snapshot stream")
        check(DELAY_MS - 5 <= mean_hold <= DELAY_MS + JITTER_MS + 30, "the delay in the path matches the test's setting")

        print(f"\n  sample: t={samples[0][0]:.2f}s since={samples[0][1]} shield={samples[0][2]}"
              f"  ->  t={samples[-1][0]:.2f}s since={samples[-1][1]} shield={samples[-1][2]}")
    finally:
        proxy.stop()
        sock.close()
        srv.terminate()
        try:
            srv.wait(timeout=5)
        except subprocess.TimeoutExpired:
            srv.kill()
        log.close()

    if failures:
        print(f"\nshield regen wire: {len(failures)} check(s) FAILED")
        return 1
    print("\nshield regen wire: all checks ok (real server, real UDP, delayed path)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
