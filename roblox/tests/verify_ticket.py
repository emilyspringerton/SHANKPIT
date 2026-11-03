#!/usr/bin/env python3
"""Verify a ticket IDUNA minted, the way the SHANKPIT C server does.

The C implementation (shankpit-460's packages/common/hmac_sha256.h) is not in this checkout, so this is
a faithful reimplementation of the documented contract, not a call into it: HMAC-SHA256 over the 20-byte
payload, truncated to 16 bytes, compared constant-time, keyed by the RAW BYTES of the env string (the
handler does []byte(os.Getenv("SHANKPIT_TICKET_SECRET")) -- the secret is never hex/base64-decoded).

Usage: verify_ticket.py <answer.json> <secret> [expected-player-id]
"""
import binascii
import hashlib
import hmac
import json
import sys
import time


def verify(path, secret_text, expect_pid=None):
    answer = json.load(open(path))
    raw = binascii.unhexlify(answer["ticket"])
    assert len(raw) == 36, f"ticket is {len(raw)} bytes, not 36"
    payload, mac = raw[:20], raw[20:]
    want = hmac.new(secret_text.encode("utf-8"), payload, hashlib.sha256).digest()[:16]
    assert hmac.compare_digest(mac, want), "MAC does not verify under the shared secret"
    expires = int.from_bytes(payload[16:20], "little")
    assert expires == answer["expires_at"], "JSON expiry disagrees with the signed bytes"
    assert expires > time.time(), "ticket is already expired"
    assert len(answer["player_id"]) == 36, "player_id is not a uuid"
    if expect_pid:
        assert answer["player_id"] == expect_pid, f"ticket is for {answer['player_id']}, not {expect_pid}"
    left = expires - int(time.time())
    print(f"  MAC verified: 16-byte truncated HMAC-SHA256 over {len(payload)} payload bytes, {left} s left")
    return answer


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        raise SystemExit(2)
    verify(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
