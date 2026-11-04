// site/check.mjs -- the gate the CI job runs before publishing:  node site/check.mjs
//
// A static site with one page is easy to get wrong in ways only a browser notices -- a link that only works at the
// domain root, a stylesheet that 404s at /SHANKPIT/, a page missing its viewport so it is unreadable on a phone.
// This checks the ARTIFACT, not the source, so it catches whatever the build produced.
//
// It is deliberately dependency-free: no npm install in CI, nothing to go stale.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..');
const out = path.resolve(root, '_site');

let fail = 0;
const ok = (cond, msg) => { if (!cond) { fail++; console.error('FAIL: ' + msg); } else console.log('ok: ' + msg); };

if (!fs.existsSync(out)) { console.error('FAIL: no _site/ -- run node site/build.mjs first'); process.exit(1); }

function walk(dir) {
  const files = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) files.push(...walk(full));
    else files.push(full);
  }
  return files;
}

const files = walk(out);
const html = files.filter((f) => f.endsWith('.html'));
const rel = (f) => path.relative(out, f);

ok(fs.existsSync(path.join(out, '.nojekyll')), '.nojekyll is present (Pages must not run Jekyll over it)');
ok(fs.existsSync(path.join(out, 'index.html')), 'the site has an index.html');
ok(fs.existsSync(path.join(out, 'docs.html')), 'the doc index was generated');
ok(fs.existsSync(path.join(out, 'style.css')), 'the stylesheet was copied');

for (const file of html) {
  const text = fs.readFileSync(file, 'utf8');
  const name = rel(file);
  ok(/<title>[^<]+<\/title>/.test(text), `${name} has a title`);
  ok(/<meta name="viewport"/.test(text), `${name} has a viewport meta`);
  ok(!/http:\/\//i.test(text), `${name} loads nothing over plain http`);
  ok(text.length > 600, `${name} is a real page, not a stub`);

  // every local href/src must resolve inside _site; anything internet-facing must be https or mailto
  const refs = [...text.matchAll(/(?:href|src)="([^"]+)"/g)].map((m) => m[1]);
  for (const ref of refs) {
    if (/^(https:\/\/|mailto:|#)/i.test(ref)) continue;
    if (/^http:/i.test(ref)) { ok(false, `${name} references ${ref} over plain http`); continue; }
    if (ref.startsWith('/')) {
      // A project page is served from /SHANKPIT/: a root-absolute link points at the user page, not this repo.
      ok(false, `${name} uses the root-absolute reference ${ref} (breaks on a project page)`);
      continue;
    }
    const target = ref.split('#')[0].split('?')[0];
    if (target === '' || target === './') { ok(fs.existsSync(path.join(out, 'index.html')), `${name} -> ./ resolves`); continue; }
    ok(fs.existsSync(path.join(path.dirname(file), target)), `${name} -> ${ref} resolves`);
  }
}

// a site that ships a megabyte of accidental binary is a mistake, not a design
for (const file of files) {
  const size = fs.statSync(file).size;
  ok(size < 2 * 1024 * 1024, `${rel(file)} is ${Math.round(size / 1024)} KB (under 2 MB)`);
}

const docLinks = (fs.readFileSync(path.join(out, 'docs.html'), 'utf8').match(/github\.com\/emilyspringerton\/SHANKPIT\/blob\/master\//g) || []).length;
ok(docLinks >= 30, `the doc index links ${docLinks} documents`);

if (fail > 0) {
  console.error(`site/check: ${fail} FAILED`);
  process.exit(1);
}
console.log(`site/check OK (${html.length} pages, ${files.length} files, ${docLinks} documents linked)`);
