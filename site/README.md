# GitHub Pages CI/CD

The project page is built and published by `.github/workflows/pages.yml` on every push to `master`.

```
site/index.html   authored        the landing page
site/style.css    authored        hard edges, one accent, mono labels
site/build.mjs    build            -> _site/  (copies the two above, GENERATES docs.html, writes .nojekyll)
site/check.mjs    gate             asserts the artifact: links resolve under a project path, pages have a
                                   title + viewport, nothing is loaded over http, nothing is a stub
```

`docs.html` is generated from `docs/*.md` and `docs2/*.md`, linking each file on GitHub. Nothing is copied, so a
document has exactly one home — the same rule the engine applies to its rules.

## Local

```bash
node site/build.mjs && node site/check.mjs
```

## One-time repository setting

The workflow cannot enable Pages for itself: `actions/configure-pages`' `enablement` needs an admin token, and
`GITHUB_TOKEN` is not admin. So this is a click, once:

**Settings → Pages → Build and deployment → Source: GitHub Actions**

(or, with an authenticated `gh`: `gh api -X POST repos/emilyspringerton/SHANKPIT/pages -f build_type=workflow`)

After that every push to `master` that touches `site/`, `docs/`, `docs2/` or the workflow builds, checks and
deploys — and until then, Actions → Pages → **Run workflow** (or re-running the failed job) publishes the same
artifact without needing a new commit. The site is at **https://emilyspringerton.github.io/SHANKPIT/** — a *project* page, which is why every
local reference is relative and `check.mjs` fails a root-absolute one.

## What the workflow does, and what it refuses to do

`build` job: checkout → `node site/build.mjs` → `node site/check.mjs` → `configure-pages` → upload the artifact.
`deploy` job: `deploy-pages`, in the `github-pages` environment, exposing the URL as the run's summary.

If the check fails, nothing is uploaded and the previously published site stays live — the deploy is atomic, and a
broken build cannot replace a working page. This is deliberate: a project page that 404s its own stylesheet is
worse than a page that is a commit behind.

## Limits, named

- **Built in CI, not published.** Pages is not enabled on the repository, so only the `build` job runs. Runs
  [41748373751](https://github.com/emilyspringerton/SHANKPIT/actions/runs/41748373751) and
  [41748946384](https://github.com/emilyspringerton/SHANKPIT/actions/runs/41748946384) built `_site/` and passed
  `check.mjs` on GitHub's runners, then failed at *Is Pages enabled?* by design — 2 pages, 5 files, 37 documents
  linked, 36 KB. Nothing has been published and the URL 404s; `deploy-pages` has never run. The blocker is the
  repository setting above, not this code.
- **No wasm is built here.** The playable builds (`shankpit-460`, TRAPX) are served from WOTAN; this site links to
  them. A Pages job that compiles the engine would need the LLVM toolchain and the sibling PARENA checkout, which is
  a different machine and a different budget.
- **No custom domain.** `CNAME` is absent on purpose; adding one is a decision (and a DNS record), not a default.
