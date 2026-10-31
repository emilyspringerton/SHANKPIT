// Headless verification of the Android game bundle.
//
// Runs the real shankpit460.wasm and the real android-controls.js (in a minimal
// fake DOM) and asserts the behaviour the Android build depends on:
//
//   * the bundle's files are present and the page wires them up,
//   * the touch layer installs a virtual gamepad the wasm accepts,
//   * a tap starts a match (phase 0 -> 1) with no pointer lock and no keyboard,
//   * with no backend configured, no WebSocket is ever constructed,
//   * the offline status is shown instead of "searching",
//   * the bot match keeps simulating frames while offline.
//
// No dependencies, no browser, no Android SDK: `node android/tools/verify-web-bundle.mjs`
import { readFileSync, existsSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';

const here = dirname(fileURLToPath(import.meta.url));
const bundleDir = join(here, '..', '..', 'apps', 'shankpit-460');

let failures = 0;
const ok = (cond, label, extra = '') => {
  if (cond) {
    console.log(`  ok   ${label}`);
  } else {
    failures++;
    console.log(`  FAIL ${label}${extra ? ' -- ' + extra : ''}`);
  }
};

const read = (f) => readFileSync(join(bundleDir, f), 'utf8');

console.log('shankpit-460 android bundle');

// ---------------------------------------------------------------- files exist
const files = ['index.html', 'sp-host.js', 'android-controls.js', 'shankpit460.wasm'];
for (const f of files) ok(existsSync(join(bundleDir, f)), `bundle has ${f}`);

const html = read('index.html');
const host = read('sp-host.js');
const controls = read('android-controls.js');

ok(/<script src="android-controls\.js"><\/script>/.test(html), 'page loads android-controls.js as a classic script');
ok(/<script type="module" src="sp-host\.js"><\/script>/.test(html), 'page loads sp-host.js as a module');
ok(html.indexOf('<script src="android-controls.js">') < html.indexOf('<script type="module" src="sp-host.js">'),
  'controls load before the generated host runs');

// The generated host looks these up by id; a missing one is a silent dead control.
const ids = ['sp-canvas', 'sp-load', 'sp-load-msg', 'sp-pill', 'sp-online', 'sp-offline',
  'sp-mute', 'sp-stat', 'sp-who', 'sp-who-msg', 'sp-who-link', 'sp-stage', 'sp-stick',
  'sp-knob', 'sp-buttons', 'btn-fire', 'btn-jump', 'btn-crouch', 'btn-reload', 'btn-ability', 'btn-swap'];
const missing = ids.filter((id) => !html.includes(`id="${id}"`));
ok(missing.length === 0, 'page defines every id the host and controls touch', missing.join(','));

const wasmBytes = readFileSync(join(bundleDir, 'shankpit460.wasm'));
ok(wasmBytes.subarray(0, 4).toString('hex') === '0061736d', 'shankpit460.wasm has the wasm magic');
console.log(`  info wasm sha256 ${createHash('sha256').update(wasmBytes).digest('hex').slice(0, 16)} (${Math.round(wasmBytes.length / 1024)} KB)`);

// A budget guard: the Android asset must stay small enough to ship in an APK.
ok(wasmBytes.length < 2 * 1024 * 1024, 'wasm module is under 2 MB');

// ------------------------------------------------------- minimal fake DOM
class FakeEl {
  constructor(id) {
    this.id = id;
    this.attrs = {};
    this.textContent = '';
    this.hidden = false;
    this.style = {};
    this.classList = { add() {}, remove() {} };
    this.listeners = {};
  }
  getAttribute(n) { return this.attrs[n]; }
  addEventListener(t, fn) { (this.listeners[t] ||= []).push(fn); }
  setPointerCapture() {}
  getBoundingClientRect() { return { left: 0, top: 0, width: 400, height: 400 }; }
  dispatchEvent() { return true; }
  fire(type, ev) { for (const fn of this.listeners[type] || []) fn(ev); }
}

const els = new Map();
const getEl = (id) => {
  if (!els.has(id)) els.set(id, new FakeEl(id));
  return els.get(id);
};
for (const id of ids) getEl(id);
getEl('btn-fire').attrs['data-btn'] = '7';
getEl('btn-jump').attrs['data-btn'] = '0';
getEl('btn-crouch').attrs['data-btn'] = '1';
getEl('btn-reload').attrs['data-btn'] = '2';
getEl('btn-ability').attrs['data-btn'] = '6';
getEl('btn-swap').attrs['data-btn'] = '5';

const buttonEls = ['btn-fire', 'btn-jump', 'btn-crouch', 'btn-reload', 'btn-ability', 'btn-swap'].map(getEl);
const timers = [];
let observerCb = null;
let realSocketsConstructed = 0;

const sandbox = {
  console,
  URLSearchParams,
  setTimeout: (fn) => { timers.push(fn); return timers.length; },
  clearTimeout: () => {},
  location: { search: '' },
  navigator: { getGamepads: () => [] },
  document: {
    hidden: false,
    getElementById: getEl,
    querySelectorAll: (sel) => (sel.includes('#sp-buttons') ? buttonEls : []),
    addEventListener() {},
  },
  MutationObserver: class {
    constructor(cb) { observerCb = cb; }
    observe() {}
  },
  MouseEvent: class {
    constructor(type) { this.type = type; }
  },
  performance: { now: () => 0 },
  requestAnimationFrame: () => 0,
  addEventListener() {},
  removeEventListener() {},
};
sandbox.window = sandbox;
sandbox.WebSocket = function RealSocket() {
  realSocketsConstructed++;
  throw new Error('offline build must not open a socket');
};

vm.createContext(sandbox);
new vm.Script(controls, { filename: 'android-controls.js' }).runInContext(sandbox);

// ------------------------------------------------------------ offline shim
console.log('offline (no backend)');
const api = sandbox.SPAndroidPad;
ok(!!api, 'controls expose SPAndroidPad');
ok(api.offline === true, 'no backend configured means offline mode');
const gps = sandbox.navigator.getGamepads();
ok(Array.isArray(gps) && gps.length === 4 && gps[0] && gps[0].axes.length === 4, 'getGamepads() installs one synthetic pad');
ok(gps[0].buttons.length === 16, 'synthetic pad exposes 16 buttons');
ok(getEl('sp-offline').hidden === false, 'offline chip becomes visible');

let socketOk = true;
try { new sandbox.WebSocket('ws://example.invalid/play'); } catch { socketOk = false; }
ok(socketOk, 'offline WebSocket stub constructs without throwing');
ok(realSocketsConstructed === 0, 'offline build never touches the network', `constructed=${realSocketsConstructed}`);

// status pill rewording
getEl('sp-pill').textContent = 'Searching for a match';
if (observerCb) observerCb();
ok(getEl('sp-pill').textContent === 'Bot match (no matchmaking)', 'offline status replaces "searching"', getEl('sp-pill').textContent);

// ---------------------------------------------------------------- wasm + pad
console.log('wasm driven through the virtual pad');
const mod = new WebAssembly.Module(wasmBytes);
ok(WebAssembly.Module.imports(mod).length === 0, 'wasm has zero imports (nothing for the host to provide)');
const ex = new WebAssembly.Instance(mod, {}).exports;
ex.sp_init();

const maskOf = (pad) => {
  let m = 0;
  for (let i = 0; i < 16; i++) if (pad.buttons[i].pressed) m |= 1 << i;
  return m;
};
// Mirrors sp-host.js sendPad(): axes 0..3, LT value, RT value, button mask.
const feed = (pad) => ex.sp_pad(0, pad.axes[0], pad.axes[1], pad.axes[2], pad.axes[3],
  pad.buttons[6].value, pad.buttons[7].value, maskOf(pad));

let t = 0;
const step = (frames) => { for (let i = 0; i < frames; i++) { feed(api.pad); ex.sp_frame(t); t += 16; } };

ok(ex.sp_phase() === 0, 'starts on the title screen');
ok(ex.bot_count() === 7, 'bot match is populated', String(ex.bot_count()));

// A real touch on the stick is what starts a match; startOnce() is that path.
api.startOnce();
// The real page holds A for ~120ms, i.e. across several animation frames; step
// first so the press is visible to a frame, then let the release timer fire.
step(4);
for (const fn of timers.splice(0)) fn();
step(120);
ok(ex.sp_phase() === 1, 'a touch starts a match (phase 1)', `phase=${ex.sp_phase()}`);
// The live sim counter only moves once the round is actually running (the title
// -> match transition includes a countdown), so give it a few seconds of frames.
step(320);
ok(ex.sp_ticks() > 0, 'the simulation advances frames', `ticks=${ex.sp_ticks()}`);
ok(ex.sp_net_wanted() === 1 || ex.sp_net_status() === 1, 'matchmaking is requested but unanswered (offline default)');
ok(realSocketsConstructed === 0, 'no socket was opened while playing');

// stick + fire through the same snapshot the host reads
api.pad.axes[1] = -1;
step(60);
api.pad.axes[0] = 1;
step(60);
api.press(7); // fire (RT)
step(60);
ok(api.pad.buttons[7].pressed === true, 'fire button latches as RT');
api.release(7);
step(30);
ok(api.pad.buttons[7].pressed === false, 'fire releases cleanly');
ok(api.pad.axes[0] === 1 && api.pad.axes[1] === -1, 'stick axes survive frames (host reads them every tick)');
ok(ex.sp_phase() === 1, 'match still running after input', `phase=${ex.sp_phase()}`);

api.releaseAll();
ok(api.pad.axes.every((v) => v === 0), 'releaseAll() clears the sticks (no stuck movement on background)');
ok(api.pad.buttons.every((b) => !b.pressed), 'releaseAll() clears every button');

// ---------------------------------------------------------------- button map
console.log('button mapping');
const mapping = {
  0: 'jump', 1: 'crouch', 2: 'reload', 5: 'next weapon', 6: 'ability', 7: 'fire',
};
for (const [idx] of Object.entries(mapping)) {
  api.press(Number(idx));
  ok(api.pad.buttons[Number(idx)].pressed, `button ${idx} (${mapping[idx]}) presses`);
  api.release(Number(idx));
}

console.log(failures === 0 ? '\nPASS' : `\nFAIL (${failures})`);
process.exit(failures === 0 ? 0 : 1);
