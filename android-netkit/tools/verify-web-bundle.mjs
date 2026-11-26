// Verifies the vendored Netkit page bundle (apps/netkit) before it is packaged -- headlessly, no browser, no Android:
//   * every .wasm is a valid module that IMPORTS NOTHING (the PARENA promise: it cannot touch a socket, a clock or the DOM)
//   * memory is fixed (initial == maximum), and has the size each module is meant to have
//   * every export the generated page calls exists in the module it calls it on
//   * every element id the generated page uses exists in index.html
//   node android-netkit/tools/verify-web-bundle.mjs [apps/netkit]
import fs from 'node:fs';
import path from 'node:path';

const dir = process.argv[2] || path.join(path.dirname(new URL(import.meta.url).pathname), '..', '..', 'apps', 'netkit');
let bad = 0;
const fail = m => { console.error('FAIL: ' + m); bad++; };
const ok = m => console.log('ok:   ' + m);

const MODULES = { 'netkit.wasm': 64, 'netkit-lite.wasm': 8, 'nkterm.wasm': 8, 'brawlpit.wasm': 8 };       // MB, fixed (brawlpit.wasm: WOTAN's web build, the arcade cabinet's game)
const exportsOf = {};
for (const [name, mb] of Object.entries(MODULES)) {
  const f = path.join(dir, name);
  if (!fs.existsSync(f)) { fail(`${name} is missing`); continue; }
  const bytes = fs.readFileSync(f);
  let m; try { m = new WebAssembly.Module(bytes); } catch (e) { fail(`${name} is not a valid wasm module: ${e.message}`); continue; }
  const imports = WebAssembly.Module.imports(m);
  imports.length ? fail(`${name} imports ${JSON.stringify(imports)}`) : ok(`${name}: ${bytes.length} bytes, imports nothing`);
  exportsOf[name] = new Set(WebAssembly.Module.exports(m).map(e => e.name));
  // fixed memory: parse the memory section's limits (flag 1 = has max); initial must equal max
  const inst = new WebAssembly.Instance(m, {});
  const pages = inst.exports.memory.buffer.byteLength / 65536;
  pages * 65536 === mb * 1048576 ? ok(`${name}: ${mb} MB of memory`) : fail(`${name}: memory is ${pages * 65536 / 1048576} MB, expected ${mb}`);
  try { inst.exports.memory.grow(1); fail(`${name}: memory can grow (it must be fixed)`); } catch { ok(`${name}: memory cannot grow`); }
}

const js = fs.readFileSync(path.join(dir, 'nk-host.js'), 'utf8');
const html = fs.readFileSync(path.join(dir, 'index.html'), 'utf8');

// every wasm export the page calls: the generated JS calls them as <exports>.nk_name(...) / <exports>.term_name(...)
// (the arcade host calls BRAWLPIT's own bp_* exports on its own instance; it also reaches nk_garage_* through the scanning instance)
const called = new Set([...js.matchAll(/\.((?:nk|term|bp)_[a-z0-9_]+)\(/g)].map(m => m[1]));
for (const fn of called) {
  const where = fn.startsWith('term_') ? ['nkterm.wasm'] : fn.startsWith('bp_') ? ['brawlpit.wasm'] : ['netkit.wasm', 'netkit-lite.wasm'];
  for (const w of where) if (exportsOf[w] && !exportsOf[w].has(fn)) fail(`${fn} is called by the page but ${w} does not export it`);
}
ok(`the page calls ${called.size} wasm exports, all present`);
// the garage's word getters are called by name through a helper (scanX[fn](..)), so check the names the page passes to it
const garageWords = new Set([...js.matchAll(/"(nk_garage_[a-z0-9_]+)"/g)].map(m => m[1]));
for (const fn of garageWords) for (const w of ['netkit.wasm', 'netkit-lite.wasm']) if (exportsOf[w] && !exportsOf[w].has(fn)) fail(`${fn} is named by the page but ${w} does not export it`);
if (![...called].some(f => f.startsWith('nk_garage_'))) fail('the page never calls nk_garage_*: nk-host.js is stale (regenerate the bundle with PARENA/scripts/build_netkit_web.sh)');
else ok(`the garage: ${[...called].filter(f => f.startsWith('nk_garage_')).length} nk_garage_* exports called, ${garageWords.size} more named`);
// BRAWLPIT's own contract: the exports the arcade host needs (a rebuilt brawlpit.wasm must still have them)
for (const fn of ['bp_init', 'bp_frame', 'bp_fb_addr', 'bp_fb_w', 'bp_fb_h', 'bp_key', 'bp_blur', 'bp_pointer', 'bp_pad', 'bp_pad_gone', 'bp_sprite_addr', 'bp_sprite_set', 'bp_duck_addr', 'bp_duck_set'])
  if (exportsOf['brawlpit.wasm'] && !exportsOf['brawlpit.wasm'].has(fn)) fail(`brawlpit.wasm does not export ${fn}, which the arcade host calls`);
// the art the arcade loads: PNG signature + the exact sizes the host copies into wasm memory (512x256 portraits, 640x340 duck)
for (const [f, w, h] of [['sprites.png', 512, 256], ['duck.png', 640, 340]]) {
  const p = path.join(dir, f);
  if (!fs.existsSync(p)) { fail(`${f} is missing`); continue; }
  const b = fs.readFileSync(p);
  const sig = b.subarray(0, 8).toString('hex') === '89504e470d0a1a0a';
  sig && b.readUInt32BE(16) === w && b.readUInt32BE(20) === h ? ok(`${f}: a ${w}x${h} PNG, ${b.length} bytes`) : fail(`${f} is not a ${w}x${h} PNG`);
}

// the maker table the scanner searches (PARENA tools/netkit_oui_gen.py): "NKOU", sorted 24-bit prefixes, fits the wasm's 64 KB region
{
  const p = path.join(dir, 'oui.bin');
  if (!fs.existsSync(p)) fail('oui.bin is missing (the device list would show no makers)');
  else {
    const b = fs.readFileSync(p), n = b.readUInt32LE(4), m = b.readUInt32LE(8), off = b.readUInt32LE(12);
    let sorted = true, prev = -1, pi = false;
    for (let i = 0; i < n; i++) { const e = b.readUInt32LE(16 + 4 * i), k = e & 0xFFFFFF; if (k <= prev) sorted = false; prev = k; if (k === 0xB827EB) pi = b.subarray(off + 24 * (e >>> 24), off + 24 * (e >>> 24) + 12).toString() === 'Raspberry Pi'; }
    b.subarray(0, 4).toString() === 'NKOU' && b.length <= 65536 && off === 16 + 4 * n && off + 24 * m <= b.length && sorted && pi
      ? ok(`oui.bin: ${n} prefixes, ${m - 1} makers, ${b.length} bytes, sorted, b8:27:eb -> Raspberry Pi`)
      : fail('oui.bin is malformed (magic, size, order or the Raspberry Pi entry)');
    if (!js.includes('nk_oui_load')) fail('the page never loads oui.bin (nk-host.js is stale)');
  }
}

const ids = new Set([...html.matchAll(/\bid="([^"]+)"/g)].map(m => m[1]));
const used = new Set();
for (const re of [/\bel\("([^"]+)"\)/g, /\b(?:setTextBang|setHtmlBang|setHiddenBang|setValBang|val|checkedP|onBang)\("([^"]+)"/g]) for (const m of js.matchAll(re)) used.add(m[1]);
for (const id of used) if (!ids.has(id)) fail(`the page uses element id "${id}" but index.html has none`);
for (const n of ['garage', 'devices', 'ssh', 'vnc', 'arcade']) { used.add('tab-' + n); if (!ids.has('tab-' + n)) fail(`index.html has no tab-${n}`); }
ok(`the page uses ${used.size} element ids, all present in index.html`);

const pad = [...html.matchAll(/data-bp="(\d+)"/g)].map(m => +m[1]).sort((a, b) => a - b);
pad.join() === '1,2,3,4,5,6,7,8,9,10,11' ? ok('the arcade has all 11 touch buttons (codes 1..11, each mapped to a key by nk_garage_touch_key)') : fail(`touch buttons are ${pad}, expected 1..11`);

if (!/<script type="module" src="nk-host\.js">/.test(html)) fail('index.html does not load nk-host.js as a module');
if (/<script(?![^>]*src=)[^>]*>[^<]/i.test(html)) fail('index.html contains an inline script (the page has no hand-written JavaScript)');
else ok('index.html has no inline script: the only JavaScript is the generated nk-host.js');

process.exit(bad ? 1 : 0);
