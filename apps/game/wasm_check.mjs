// Boots bin/shank_core.wasm the way a browser would: no imports, no shims, real exported calls.
// Proves the core-only capability mask cannot be talked into enabling render/audio/sky modules.
import { readFileSync } from "node:fs";
import assert from "node:assert/strict";

const bytes = readFileSync(new URL("../../bin/shank_core.wasm", import.meta.url));
const { instance } = await WebAssembly.instantiate(bytes, {});
const core = instance.exports;

assert.equal(typeof core.wasm_boot, "function", "the module exports wasm_boot");

const capabilities = core.wasm_boot();
assert.equal(capabilities, 3, "core build declares core sim + level only");
assert.equal(core.wasm_net_status(), 0, "the WASM core links no network transport");
assert.equal(core.wasm_profile_target(), 2, "the WASM build reports the wasm-core profile");
assert.equal(core.wasm_module_count(), 5, "all five module ids still exist in the rules");

assert.equal(core.wasm_register_level(21, 0), 3, "surface opens at the core-only mask");
assert.equal(core.wasm_mask(21), 3);
assert.equal(core.wasm_enabled(21, 3), 0, "weather render is not ported");
assert.equal(core.wasm_set_mod(21, 3, 1), 0, "a core build refuses to enable weather render");
assert.equal(core.wasm_mask(21), 3, "the refused update left the mask alone");

assert.equal(core.wasm_register_level(22, 2), 3, "a moon level opens core-only too");
assert.equal(core.wasm_apply_state(22, 31, 2), 0, "incoming 31 cannot add unported modules");
assert.equal(core.wasm_mask(22), 3, "state is clamped to the ported modules");
assert.equal(core.wasm_revision(22), 2, "a newer state still advances the revision");
assert.equal(core.wasm_apply_state(22, 7, 1), 0, "an older state cannot roll the level back");
assert.equal(core.wasm_mask(22), 3);

assert.equal(core.wasm_level_count(), 2);
assert.ok(core.wasm_event_count() >= 2, "registrations reached the REFLUX log");

// The module carries its own sin/cos/atan2 (no libm, no imports). Compare against JS Math.
assert.ok(Math.abs(core.wasm_sin_deg(30) - 0.5) < 1e-9, "freestanding sin(30 deg)");
assert.ok(Math.abs(core.wasm_cos_deg(90)) < 1e-9, "freestanding cos(90 deg)");
assert.ok(Math.abs(core.wasm_atan2(1, 1) - Math.PI / 4) < 1e-6, "freestanding atan2(1,1)");

// ---- playable browser ABI ---------------------------------------------------------------------
assert.equal(typeof core.wasm_play_boot, "function", "the module exports the browser boot");
assert.equal(typeof core.wasm_frame, "function", "the module exports a frame step");
assert.equal(typeof core.memory, "object", "linear memory is exported for the canvas");

const playCaps = core.wasm_play_boot(31, 2);
assert.equal(playCaps, 3, "the playable core still admits only core sim + level");
assert.equal(core.wasm_level(), 31, "the browser boot registers its level");
assert.equal(core.wasm_fb_width(), 1024);
assert.equal(core.wasm_fb_height(), 576);

const fb = new Uint32Array(core.memory.buffer, core.wasm_fb_ptr(), 1024 * 576);
const colors = new Set();
for (let i = 0; i < fb.length; i += 997) colors.add(fb[i]);
assert.ok(colors.size >= 4, `the first frame drew ${colors.size} sampled colours`);

const x0 = core.wasm_player_x();
const z0 = core.wasm_player_z();
assert.equal(x0, 0);
assert.equal(z0, -700);
assert.equal(core.wasm_set_input(100, 0), 0);
assert.equal(core.wasm_frame(16), 1, "16 ms is one fixed tick");
assert.ok(core.wasm_player_z() > z0, "forward input moves the body down +Z");
assert.equal(core.wasm_player_x(), x0, "forward at yaw 0 does not drift sideways");

const z1 = core.wasm_player_z();
core.wasm_turn(1571);
assert.ok(core.wasm_player_yaw() > 1500, "turn applies a yaw delta");
core.wasm_frame(16);
assert.ok(core.wasm_player_x() > 0, "after a right turn, forward input moves +X");
assert.ok(core.wasm_player_z() >= z1, "forward does not reverse while turning");

assert.equal(core.wasm_frame(250), 6, "a stalled frame is capped at the six-tick budget");
core.wasm_set_input(0, 0);
console.log(
  `wasm-smoke OK: caps=${capabilities} modules=${core.wasm_module_count()} ` +
    `levels=${core.wasm_level_count()} events=${core.wasm_event_count()} ` +
    `fb=${core.wasm_fb_width()}x${core.wasm_fb_height()} ticks=${core.wasm_ticks()} ` +
    `colours=${colors.size}`
);
