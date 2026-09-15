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
# Real, honest, deliberate v0 narrowing vs. BRAWLPIT's own Python bot pool: no multi-checkpoint
# league pool (SHANKPIT has no league yet to draw a pool from) -- every bot here runs emily-bot's
# own already-real heuristic aim-and-shoot AI (nearest-peer targeting, weapon-range-aware
# approach/retreat), the same fallback path emily-bot already uses whenever no -gpt2-url/
# -archetype-engine is configured. -no-report disables Emily Prime kill/event HTTP reporting
# (that reporting exists for EMILY_PRIME's own single-agent self-play sessions, not ordinary
# queue-filler bots).
#
# S459-62, founder real-time: "can we update the QUEUE to use the active opponent until we have a
# league to queue against?" -- real, deliberate STOPGAP, checked once at startup (not a live
# re-poll loop -- restart this service to pick up a newly-activated opponent, a real, honest v0
# boundary rather than the added complexity of hot-swapping bot processes mid-session): if IDUNA
# has a real "active opponent" checkpoint set (GET /api/v1/shankpit-checkpoints/active,
# internal/shankpit.CheckpointStore.SetActiveOpponent, wired to NOCK's "Set as opponent" button),
# this pool downloads it once and launches $BOT_COUNT real scripts/frozen_policy_bot.py
# processes (real PPO inference over the same real UDP wire protocol, S459-54's own self-play
# primitive) instead of the heuristic emily-bot pool. Falls back to the original heuristic pool
# on ANY failure along the way (registry unreachable, no active opponent set, download failed) --
# never a half-launched pool. This is NOT BRAWLPIT's own native in-game C inference (SHANKPIT has
# no has_weights/native-export concept at all yet, real separate future work) -- a real Python
# subprocess per bot, same class of primitive training already uses, not silently promised as
# more than that.
#
# Usage: ops/shankpit-bot-pool.sh [host] [port] [bot_count]
#   ops/shankpit-bot-pool.sh                      # 127.0.0.1:6969, 3 bots (S459-34's own "3 bots")
#   ops/shankpit-bot-pool.sh game.example.com 6969 3

set -euo pipefail

HOST="${1:-127.0.0.1}"
PORT="${2:-6969}"
BOT_COUNT="${3:-3}"
SHANKPIT_MODE_QUEUE=108
IDUNA_BASE_URL="${IDUNA_BASE_URL:-https://okemily.com}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
BOT_BIN="$REPO_ROOT/bin/emily-bot"
CACHE_DIR="$REPO_ROOT/var/bot_pool_checkpoints"

if [ ! -x "$BOT_BIN" ]; then
    echo "shankpit-bot-pool: building emily-bot ($BOT_BIN not found)..."
    (cd "$REPO_ROOT" && GOWORK=off go build -o bin/emily-bot ./apps2/emily-bot)
fi

ACTIVE_PATH=""
ACTIVE_ID=""
if ACTIVE_ID="$(cd "$REPO_ROOT" && python3 scripts/rl_registry.py active --base-url "$IDUNA_BASE_URL" 2>/dev/null)"; then
    mkdir -p "$CACHE_DIR"
    CANDIDATE="$CACHE_DIR/active_${ACTIVE_ID}.zip"
    if [ -f "$CANDIDATE" ]; then
        ACTIVE_PATH="$CANDIDATE"
    elif (cd "$REPO_ROOT" && python3 scripts/rl_registry.py pull --base-url "$IDUNA_BASE_URL" "$ACTIVE_ID" "$CANDIDATE" >/dev/null 2>&1); then
        ACTIVE_PATH="$CANDIDATE"
    else
        echo "shankpit-bot-pool: WARNING: active opponent id=$ACTIVE_ID set but download failed -- falling back to the heuristic pool"
    fi
fi

pids=()
cleanup() {
    echo "shankpit-bot-pool: stopping ${#pids[@]} bot(s)..."
    for pid in "${pids[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
}
trap cleanup EXIT INT TERM

if [ -n "$ACTIVE_PATH" ]; then
    echo "shankpit-bot-pool: real active opponent set (checkpoint id=$ACTIVE_ID, $CANDIDATE) -- launching $BOT_COUNT real frozen-policy bot(s) -> $HOST:$PORT instead of the heuristic pool"
    for i in $(seq 1 "$BOT_COUNT"); do
        # No --session-duration -- frozen_policy_bot.py's own default (1e9s, effectively forever)
        # already gives the same real "standing daemon, no churn-reconnect" behavior the
        # heuristic branch below needs -session-duration 87600h to get explicitly.
        (cd "$REPO_ROOT" && python3 scripts/frozen_policy_bot.py --host "$HOST" --port "$PORT" --checkpoint "$ACTIVE_PATH") &
        pids+=("$!")
        echo "shankpit-bot-pool: frozen-policy bot $i pid=$! (checkpoint id=$ACTIVE_ID)"
    done
else
    echo "shankpit-bot-pool: no active opponent set -- launching $BOT_COUNT real heuristic packet-level bot(s) -> $HOST:$PORT mode=$SHANKPIT_MODE_QUEUE (queue)"
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
fi

wait
