#!/usr/bin/env bash
# gen_trapx_parity.sh -- regenerate PARENA/stdlib/trapx/trapx_parity.txt FROM THE C.
#
# Rows are never hand-written. This compiles a small dumper against packages/simulation/trapx_rules.c (the C the
# SHANKPIT host links), asks it the questions, and writes the answers where BOTH worlds read them:
# trapx_test.c here, and WOTAN/scripts/trapx-rules-test.mjs against the generated JavaScript. A row that disagrees
# with the rules means the .prn changed and the fixture has not been regenerated -- run this, do not edit the file.
#
# Format: question a b c d | answer   (six integers; unused arguments are 0)
# Usage: scripts/gen_trapx_parity.sh
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
parena_dir="${PARENA_DIR:-$root/../PARENA}"
out="$parena_dir/stdlib/trapx/trapx_parity.txt"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cat > "$tmp/dump.c" <<'C'
#include <stdio.h>
#include "trapx_rules.c"

static void row(int q, int a, int b, int c, int d, int answer) {
    printf("%d %d %d %d %d | %d\n", q, a, b, c, d, answer);
}

int main(void) {
    /* 0 scene-fos */
    row(0, 200, 0, 0, 0, trapx_scene_fos(200));
    row(0, 201, 0, 0, 0, trapx_scene_fos(201));
    row(0, 999, 0, 0, 0, trapx_scene_fos(999));
    /* 1 scene-watcher-density */
    row(1, 200, 0, 0, 0, trapx_scene_watcher_density(200));
    row(1, 201, 0, 0, 0, trapx_scene_watcher_density(201));
    row(1, 0, 0, 0, 0, trapx_scene_watcher_density(0));
    /* 2 scene-district */
    row(2, 200, 0, 0, 0, trapx_scene_district(200));
    row(2, 201, 0, 0, 0, trapx_scene_district(201));
    row(2, 7, 0, 0, 0, trapx_scene_district(7));
    /* 3 portal-target */
    row(3, 200, 0, 0, 0, trapx_portal_target(200));
    row(3, 201, 0, 0, 0, trapx_portal_target(201));
    row(3, 300, 0, 0, 0, trapx_portal_target(300));
    /* 4 scene-known */
    row(4, 200, 0, 0, 0, trapx_scene_known(200));
    row(4, 201, 0, 0, 0, trapx_scene_known(201));
    row(4, 199, 0, 0, 0, trapx_scene_known(199));
    /* 5 alertness-step (alertness, density, heat, quiet) */
    row(5, 0, 900, 0, 0, trapx_alertness_step(0, 900, 0, 0));
    row(5, 0, 400, 0, 0, trapx_alertness_step(0, 400, 0, 0));
    row(5, 0, 900, 50, 0, trapx_alertness_step(0, 900, 50, 0));
    row(5, 0, 900, 0, 1, trapx_alertness_step(0, 900, 0, 1));
    row(5, 95, 900, 100, 0, trapx_alertness_step(95, 900, 100, 0));
    row(5, 0, 0, 0, 1, trapx_alertness_step(0, 0, 0, 1));
    /* 6 trust-step (trust, clean, dirty) */
    row(6, 0, 1, 0, 0, trapx_trust_step(0, 1, 0));
    row(6, 0, 0, 1, 0, trapx_trust_step(0, 0, 1));
    row(6, 0, 3, 3, 0, trapx_trust_step(0, 3, 3));
    row(6, 95, 10, 0, 0, trapx_trust_step(95, 10, 0));
    /* 7 enforcement-level (alertness, trust, heat) */
    row(7, 0, 0, 0, 0, trapx_enforcement_level(0, 0, 0));
    row(7, 65, 0, 0, 0, trapx_enforcement_level(65, 0, 0));
    row(7, 60, -25, 10, 0, trapx_enforcement_level(60, -25, 10));
    row(7, 60, 0, 55, 0, trapx_enforcement_level(60, 0, 55));
    row(7, 80, 0, 65, 0, trapx_enforcement_level(80, 0, 65));
    row(7, 89, 0, 76, 0, trapx_enforcement_level(89, 0, 76));
    row(7, 97, -70, 91, 0, trapx_enforcement_level(97, -70, 91));
    /* 8 media-press (enforcement, alertness) */
    row(8, 0, 74, 0, 0, trapx_media_press(0, 74));
    row(8, 2, 0, 0, 0, trapx_media_press(2, 0));
    row(8, 0, 75, 0, 0, trapx_media_press(0, 75));
    /* 9 heat-step (heat, action, enforcement) */
    row(9, 0, 0, 0, 0, trapx_heat_step(0, 0, 0));
    row(9, 50, 0, 0, 0, trapx_heat_step(50, 0, 0));
    row(9, 50, 1, 0, 0, trapx_heat_step(50, 1, 0));
    row(9, 50, 2, 0, 0, trapx_heat_step(50, 2, 0));
    row(9, 50, 2, 1, 0, trapx_heat_step(50, 2, 1));
    row(9, 99, 2, 2, 0, trapx_heat_step(99, 2, 2));
    row(9, 99, 4, 0, 0, trapx_heat_step(99, 4, 0));
    /* 10 claim-cost (scene, owned) */
    row(10, 200, 0, 0, 0, trapx_claim_cost(200, 0));
    row(10, 200, 1, 0, 0, trapx_claim_cost(200, 1));
    row(10, 201, 0, 0, 0, trapx_claim_cost(201, 0));
    row(10, 201, 3, 0, 0, trapx_claim_cost(201, 3));
    /* 11 flow-per-tick (integrity, enforcement) */
    row(11, 100, 0, 0, 0, trapx_flow_per_tick(100, 0));
    row(11, 100, 1, 0, 0, trapx_flow_per_tick(100, 1));
    row(11, 40, 2, 0, 0, trapx_flow_per_tick(40, 2));
    row(11, 0, 0, 0, 0, trapx_flow_per_tick(0, 0));
    /* 12 pressure-per-tick (attention, enforcement, fos) */
    row(12, 80, 0, 1, 0, trapx_pressure_per_tick(80, 0, 1));
    row(12, 0, 0, 0, 0, trapx_pressure_per_tick(0, 0, 0));
    row(12, 0, 2, 3, 0, trapx_pressure_per_tick(0, 2, 3));
    /* 13 attention-step (attention, k9) */
    row(13, 0, 2, 0, 0, trapx_attention_step(0, 2));
    row(13, 50, 0, 0, 0, trapx_attention_step(50, 0));
    row(13, 95, 1, 0, 0, trapx_attention_step(95, 1));
    row(13, 2, 0, 0, 0, trapx_attention_step(2, 0));
    /* 14 k9-worth (deployed) */
    row(14, 0, 0, 0, 0, trapx_k9_worth(0));
    row(14, 1, 0, 0, 0, trapx_k9_worth(1));
    row(14, 2, 0, 0, 0, trapx_k9_worth(2));
    row(14, 3, 0, 0, 0, trapx_k9_worth(3));
    row(14, 4, 0, 0, 0, trapx_k9_worth(4));
    row(14, 5, 0, 0, 0, trapx_k9_worth(5));
    row(14, 9, 0, 0, 0, trapx_k9_worth(9));
    /* 15 contest-window-ms (pressure) */
    row(15, 0, 0, 0, 0, trapx_contest_window_ms(0));
    row(15, 40, 0, 0, 0, trapx_contest_window_ms(40));
    row(15, 100, 0, 0, 0, trapx_contest_window_ms(100));
    /* 16 integrity-decay (integrity, attention, k9) */
    row(16, 100, 0, 0, 0, trapx_integrity_decay(100, 0, 0));
    row(16, 100, 50, 2, 0, trapx_integrity_decay(100, 50, 2));
    row(16, 10, 50, 2, 0, trapx_integrity_decay(10, 50, 2));
    row(16, 100, 100, 5, 0, trapx_integrity_decay(100, 100, 5));
    /* 17 rogue-swarm (integrity, attention, pressure) */
    row(17, 19, 80, 60, 0, trapx_rogue_swarm(19, 80, 60));
    row(17, 20, 80, 60, 0, trapx_rogue_swarm(20, 80, 60));
    row(17, 10, 90, 50, 0, trapx_rogue_swarm(10, 90, 50));
    row(17, 5, 100, 100, 0, trapx_rogue_swarm(5, 100, 100));
    /* 18 receipt-digest (kind, amount, tick) */
    row(18, 0, 0, 0, 0, trapx_receipt_digest(0, 0, 0));
    row(18, 1, 40, 7, 0, trapx_receipt_digest(1, 40, 7));
    row(18, 4, 5, 120, 0, trapx_receipt_digest(4, 5, 120));
    /* 19 receipt-known (kind) */
    row(19, 0, 0, 0, 0, trapx_receipt_known(0));
    row(19, 4, 0, 0, 0, trapx_receipt_known(4));
    row(19, 5, 0, 0, 0, trapx_receipt_known(5));
    row(19, -1, 0, 0, 0, trapx_receipt_known(-1));
    /* 20 public-kill-heat (witnesses) */
    row(20, 0, 0, 0, 0, trapx_public_kill_heat(0));
    row(20, 5, 0, 0, 0, trapx_public_kill_heat(5));
    row(20, 23, 0, 0, 0, trapx_public_kill_heat(23));
    /* 21 public-kill-trust (witnesses) */
    row(21, 0, 0, 0, 0, trapx_public_kill_trust(0));
    row(21, 5, 0, 0, 0, trapx_public_kill_trust(5));
    row(21, 20, 0, 0, 0, trapx_public_kill_trust(20));
    return 0;
}
C

gcc -std=gnu99 -O1 -Wall -Wextra -Werror -I"$root/packages/simulation" -I"$parena_dir/stdlib/trapx" \
    "$tmp/dump.c" -o "$tmp/dump"
"$tmp/dump" > "$out"
echo "regenerated PARENA/stdlib/trapx/trapx_parity.txt ($(grep -c '|' "$out") rows)"
