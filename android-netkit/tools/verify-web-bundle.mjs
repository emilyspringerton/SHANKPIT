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

const MODULES = { 'netkit.wasm': 64, 'netkit-lite.wasm': 8, 'nkterm.wasm': 8 };       // MB, fixed
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
const called = new Set([...js.matchAll(/\.((?:nk|term)_[a-z0-9_]+)\(/g)].map(m => m[1]));
for (const fn of called) {
  const where = fn.startsWith('term_') ? ['nkterm.wasm'] : ['netkit.wasm', 'netkit-lite.wasm'];
  for (const w of where) if (exportsOf[w] && !exportsOf[w].has(fn)) fail(`${fn} is called by the page but ${w} does not export it`);
}
ok(`the page calls ${called.size} wasm exports, all present`);

const ids = new Set([...html.matchAll(/\bid="([^"]+)"/g)].map(m => m[1]));
const used = new Set();
for (const re of [/\bel\("([^"]+)"\)/g, /\b(?:setTextBang|setHtmlBang|setHiddenBang|setValBang|val|checkedP|onBang)\("([^"]+)"/g]) for (const m of js.matchAll(re)) used.add(m[1]);
for (const id of used) if (!ids.has(id)) fail(`the page uses element id "${id}" but index.html has none`);
for (const n of ['devices', 'ssh', 'vnc']) { used.add('tab-' + n); if (!ids.has('tab-' + n)) fail(`index.html has no tab-${n}`); }
ok(`the page uses ${used.size} element ids, all present in index.html`);

if (!/<script type="module" src="nk-host\.js">/.test(html)) fail('index.html does not load nk-host.js as a module');
if (/<script(?![^>]*src=)[^>]*>[^<]/i.test(html)) fail('index.html contains an inline script (the page has no hand-written JavaScript)');
else ok('index.html has no inline script: the only JavaScript is the generated nk-host.js');

process.exit(bad ? 1 : 0);
