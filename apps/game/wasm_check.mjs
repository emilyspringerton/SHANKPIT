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

console.log(
  `wasm-smoke OK: caps=${capabilities} modules=${core.wasm_module_count()} ` +
    `levels=${core.wasm_level_count()} events=${core.wasm_event_count()}`
);
