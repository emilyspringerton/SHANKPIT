#!/usr/bin/env bash
# live_ticket_check.sh -- the whole chain against a REAL IDUNA, on this box, no cluster and no mocks.
#
#   1. build and boot the real IDUNA binary on a throwaway SQLite file and a private port (IDUNA_ADDR,
#      the variable main.go added for exactly this), with a throwaway SHANKPIT_TICKET_SECRET (the
#      secret the game server would hold)
#   2. sign a player up exactly the way the Roblox place does: POST /api/v1/games/shankpit/guest-register
#      -- a real players row, a real ES256 player token
#   3. mint a connect ticket with that token: POST /api/v1/shankpit/ticket
#   4. verify the ticket's MAC the way the C server does: HMAC-SHA256 over the 20-byte payload, truncated
#      to 16 bytes, constant-time compare, keyed by the RAW BYTES OF THE ENV STRING (the handler does
#      []byte(os.Getenv("SHANKPIT_TICKET_SECRET")), so the secret is not hex/base64-decoded anywhere --
#      a distinction this checker itself got wrong on its first run). Reimplemented in Python because the
#      C implementation lives in the separate shankpit-460 repo, which is NOT this checkout
#   5. hand IDUNA's OWN JSON answer to roblox/tests/live_mint_test.lua, so the module the place uses
#      proves it reads a real ticket and not a fixture
#
# Usage: roblox/scripts/live_ticket_check.sh        (needs go on PATH and a sibling ../IDUNA)
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
iduna="${IDUNA_DIR:-$root/../IDUNA}"
[ -d "$iduna" ] || { echo "live_ticket_check: no IDUNA at $iduna" >&2; exit 1; }
export PATH="$HOME/.local/go/bin:$HOME/.local/bin:/usr/local/go/bin:$PATH"

work="$(mktemp -d)"
port="${LIVE_PORT:-18099}"
secret="$(openssl rand -hex 32)"
base="http://127.0.0.1:$port"
pid=""

cleanup() { [ -n "$pid" ] && kill "$pid" 2>/dev/null || true; rm -rf "$work"; }
trap cleanup EXIT

echo "building IDUNA..."
( cd "$iduna" && go build -o "$work/iduna" . )

echo "booting IDUNA on $base (throwaway db, throwaway ticket secret)..."
# Run it FROM IDUNA's own tree: its relative var/*.db paths (blog, vault, kanban, ...) are created at
# startup, and launching from anywhere else drops ten SQLite files into whatever repo you happened to be
# standing in. Learned by doing exactly that to SHANKPIT on this script's first run.
( cd "$iduna" && SQLITE_PATH="$work/iduna.db" IDUNA_ROOT="$iduna" KEY_FILE="$work/keys.json" \
    JWT_ISSUER="$base" BASE_URL="$base" IDUNA_ADDR="127.0.0.1:$port" \
    SHANKPIT_TICKET_SECRET="$secret" exec "$work/iduna" >"$work/iduna.log" 2>&1 ) &
pid=$!

ready=0
for _ in $(seq 1 80); do
    if curl -sf -o /dev/null "$base/health"; then ready=1; break; fi
    if ! kill -0 "$pid" 2>/dev/null; then break; fi
    sleep 0.25
done
if [ "$ready" != 1 ]; then
    echo "IDUNA never became healthy; its own log:" >&2
    tail -20 "$work/iduna.log" >&2
    exit 1
fi

echo "signing a player up the way the Roblox place does..."
curl -sf -X POST "$base/api/v1/games/shankpit/guest-register" \
    -H 'Content-Type: application/json' -d '{"display_name":"RobloxCheck"}' >"$work/reg.json"
token="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["token"])' "$work/reg.json")"
player="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["player_id"])' "$work/reg.json")"
secret_got="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1])).get("guest_secret",""))' "$work/reg.json")"
display="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1])).get("display_name",""))' "$work/reg.json")"
echo "  player_id=$player name=$display"

echo "signing back in with the guest secret (the returning path)..."
curl -sf -X POST "$base/api/v1/games/shankpit/guest-login" \
    -H 'Content-Type: application/json' \
    -d "{\"player_id\":\"$player\",\"guest_secret\":\"$secret_got\"}" >"$work/login.json"
token2="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["token"])' "$work/login.json")"
[ "$token2" = "$token" ] && echo "  NOTE: identical token (same claims, same second)" || echo "  fresh token issued"

echo "minting the connect ticket..."
status="$(curl -s -o "$work/ticket.json" -w '%{http_code}' -X POST "$base/api/v1/shankpit/ticket" \
    -H "Authorization: Bearer $token2" -H 'Content-Type: application/json' -d '{}')"
if [ "$status" != "200" ]; then
    echo "ticket mint failed: HTTP $status" >&2; cat "$work/ticket.json" >&2; exit 1
fi

python3 - "$work/ticket.json" "$secret" <<'PY'
import binascii, hashlib, hmac, json, sys, time
answer = json.load(open(sys.argv[1]))
secret = sys.argv[2].encode("utf-8")  # []byte(env), exactly as shankpit_ticket.go keys the HMAC
ticket = answer["ticket"]
raw = binascii.unhexlify(ticket)
assert len(raw) == 36, f"ticket is {len(raw)} bytes, not 36"
payload, mac = raw[:20], raw[20:]
expires = int.from_bytes(payload[16:20], "little")
want = hmac.new(secret, payload, hashlib.sha256).digest()[:16]
assert hmac.compare_digest(mac, want), "MAC does not verify under the shared secret"
assert expires == answer["expires_at"], "JSON expiry disagrees with the bytes"
assert expires > time.time(), "ticket is already expired"
assert len(answer["player_id"]) == 36, "player_id is not a uuid"
print(f"MAC verified: 16-byte truncated HMAC-SHA256 over {len(payload)} payload bytes, {expires - int(time.time())} s left")
PY

echo "reading IDUNA's live answer with the place's own module..."
ticket_hex="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["ticket"])' "$work/ticket.json")"
expires_at="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["expires_at"])' "$work/ticket.json")"
player_id="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["player_id"])' "$work/ticket.json")"
( cd "$root" && lua roblox/tests/live_mint_test.lua "$ticket_hex" "$expires_at" "$player_id" )

echo "live_ticket_check: OK (real IDUNA, real ES256 auth, real ticket, verified MAC)"
