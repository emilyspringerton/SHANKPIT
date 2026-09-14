#!/bin/bash
# ops/shankpit-bot-pool.sh -- S459-34, founder real-time: "set up a bot queue... 3 bots - 4 player
# games... architect the bots the same way the bots work for brawlpit... packet level bots just
# like brawlpit."
#
# A real, standing pool of real, separate `emily-bot` PROCESSES, each opening its own real UDP
# socket and connecting to the SHANKPIT game server exactly the way a human client would (real
# PacketConnect requesting MODE_QUEUE=108, real PacketUserCmd at 20Hz) -- never an in-process
# PlayerState puppet. This is the real, literal analog of BRAWLPIT's own rl_bot_pool.py: a standing
# bot-pool daemon that queues into the SAME real matchmaker humans use via the same real wire
# packets, not a special training-only mechanism (BRAWLPIT's own doc comment, scripts/
# rl_bot_pool.py:13-16, "Mirrors REDGARDEN's own real apps/arena_bot precedent").
#
# Real, honest, deliberate v0 narrowing vs. BRAWLPIT's own Python bot pool: no RL-checkpoint
# registry (SHANKPIT has no trained checkpoints to draw from) -- every bot here runs emily-bot's
# own already-real heuristic aim-and-shoot AI (nearest-peer targeting, weapon-range-aware
# approach/retreat), the same fallback path emily-bot already uses whenever no -gpt2-url/
# -archetype-engine is configured. -no-report disables Emily Prime kill/event HTTP reporting
# (that reporting exists for EMILY_PRIME's own single-agent self-play sessions, not ordinary
# queue-filler bots).
#
# Usage: ops/shankpit-bot-pool.sh [host] [port] [bot_count]
#   ops/shankpit-bot-pool.sh                      # 127.0.0.1:6969, 3 bots (S459-34's own "3 bots")
#   ops/shankpit-bot-pool.sh game.example.com 6969 3

set -euo pipefail

HOST="${1:-127.0.0.1}"
PORT="${2:-6969}"
BOT_COUNT="${3:-3}"
SHANKPIT_MODE_QUEUE=108

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
BOT_BIN="$REPO_ROOT/bin/emily-bot"

if [ ! -x "$BOT_BIN" ]; then
    echo "shankpit-bot-pool: building emily-bot ($BOT_BIN not found)..."
    (cd "$REPO_ROOT" && GOWORK=off go build -o bin/emily-bot ./apps2/emily-bot)
fi

echo "shankpit-bot-pool: launching $BOT_COUNT real packet-level bot(s) -> $HOST:$PORT mode=$SHANKPIT_MODE_QUEUE (queue)"

pids=()
cleanup() {
    echo "shankpit-bot-pool: stopping ${#pids[@]} bot(s)..."
    for pid in "${pids[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
}
trap cleanup EXIT INT TERM

for i in $(seq 1 "$BOT_COUNT"); do
    # Real, found-live fix: emily-bot's own -session-duration defaults to 60s (a real, sensible
    # default for a bounded RL self-play SESSION, the tool's original real purpose) -- left
    # unset here, every bot in this pool was disconnecting and reconnecting (new UDP source
    # port, new server slot) every 60 seconds, a real, confirmed-live cause of "there are not
    # bots in this match" (founder real-time, 2026-09-14) whenever a human's own connect landed
    # inside one of those churn windows. Matches shankpit-460's own real, already-working
    # precedent (`ops/systemd/shankpit460-emily-bot.service`'s own `-duration 87600h`) for a
    # standing daemon bot, not a bounded test session.
    "$BOT_BIN" -host "$HOST" -port "$PORT" -mode "$SHANKPIT_MODE_QUEUE" -no-report -session-duration 87600h &
    pids+=("$!")
    echo "shankpit-bot-pool: bot $i pid=$! "
done

wait
