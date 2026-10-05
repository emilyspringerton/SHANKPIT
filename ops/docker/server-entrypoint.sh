#!/bin/sh
# Installed as /app/shank_server (real binary: /app/shank_server.real). Found live, 2026-10-05:
# /app/var is the pod's shared emptyDir (EMILY/gitops/specs/shankpit.pod, var=/app/var) -- it is
# genuinely empty at pod start, which masks whatever the image bakes in at that path. The
# "zombie" container's --level /app/var/zombie/nextown_zombies.json arg depends on that file
# existing there, so without this, it silently never loads (level_boxes_load_from_file fails
# soft and falls back to the default scene -- confirmed via a scratch pod: `ls /app/var/zombie/`
# -> No such file or directory). Seed it from the image's own baked copy on every container
# start; idempotent, so it doesn't matter which sibling container (server/zombie) wins the race.
set -e
mkdir -p /app/var/zombie
[ -f /app/var/zombie/nextown_zombies.json ] || cp /app/seed_zombie.json /app/var/zombie/nextown_zombies.json
exec /app/shank_server.real "$@"
