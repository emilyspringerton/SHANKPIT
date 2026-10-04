#!/bin/sh
# In-pod QUEUE bot pool: N heuristic packet-level emily-bots -> the server container on 127.0.0.1.
# Mirrors the heuristic branch of ops/shankpit-bot-pool.sh (the frozen-policy branch needs python+torch; not in this image).
N="${1:-4}"; PORT="${2:-6969}"
trap 'kill 0' TERM INT
i=0; while [ "$i" -lt "$N" ]; do
  /app/emily-bot -host 127.0.0.1 -port "$PORT" -mode 108 -no-report -session-duration 87600h &
  i=$((i+1))
done
wait
