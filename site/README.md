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
```## One-time repository setting (done 2026-11-04)

The workflow cannot enable Pages for itself: `actions/configure-pages`' `enablement` needs an admin token, and
`GITHUB_TOKEN` is not admin. So it was done once, by API, with an admin PAT:

```bash
curl -X POST -H "Authorization: Bearer $TOKEN" \
  https://api.github.com/repos/emilyspringerton/SHANKPIT/pages -f build_type=workflow
```

`POST /pages` answered **201** with `build_type: workflow`; `GET /pages` then read it back. After that every push
to `master` that touches `site/`, `docs/`, `docs2/` or the workflow builds, checks and deploys — and
Actions → Pages → **Run workflow** publishes the same artifact without needing a new commit, which is how the
first deploy was made. The site is at **https://emilyspringerton.github.io/SHANKPIT/** — a *project* page, which is
why every local reference is relative and `check.mjs` fails a root-absolute one.

## What the workflow does, and what it refuses to do

`build` job: checkout → `node site/build.mjs` → `node site/check.mjs` → `configure-pages` → upload the artifact.
`deploy` job: `deploy-pages`, in the `github-pages` environment, exposing the URL as the run's summary.

If the check fails, nothing is uploaded and the previously published site stays live — the deploy is atomic, and a
broken build cannot replace a working page. This is deliberate: a project page that 404s its own stylesheet is
worse than a page that is a commit behind.

## Limits, named

- **Published.** [Run 41755803522](https://github.com/emilyspringerton/SHANKPIT/actions/runs/41755803522) is the
  first real deploy: build, check, configure, upload and `deploy-pages` all green, and the live URL answers
  **200** — `index.html` 4654 B, `docs.html` 13638 B, `style.css` 4065 B, `build.json` 193 B. Before Pages was
  enabled, runs [41748373751](https://github.com/emilyspringerton/SHANKPIT/actions/runs/41748373751) and
  [41748946384](https://github.com/emilyspringerton/SHANKPIT/actions/runs/41748946384) built and checked the same
  artifact green on GitHub's runners and then failed at *Is Pages enabled?* by design, which is what the guard is
  for.
- **The outbound play links are unverified from the environment this was written in.** `index.html` links
  `https://wotan.okemily.com/shankpit-460/` and `/trapx/`, which is where those builds are served from. On
  2026-11-04 every `okemily.com` host answered *no*: the name resolves (`136.68.242.3`, note that the project's own
  records still say `198.58.107.85`) and TCP connects, but both :80 and :443 reset with zero bytes, while
  `example.com` and `github.com` return 200 from the same shell — so the origin refuses this sandbox rather than the
  network being down. The links point where the builds are meant to live; nobody has claimed here that they were
  observed serving.
- **No wasm is built here.** The playable builds (`shankpit-460`, TRAPX) are served from WOTAN; this site links to
  them. A Pages job that compiles the engine would need the LLVM toolchain and the sibling PARENA checkout, which is
  a different machine and a different budget.
- **No custom domain.** `CNAME` is absent on purpose; adding one is a decision (and a DNS record), not a default.
