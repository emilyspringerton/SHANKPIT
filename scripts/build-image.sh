#!/usr/bin/env bash
# build-image.sh [TAG] — Cloud Build the shankpit (server + emily-bot) image from tracked files.
set -euo pipefail
SRC="$(cd "$(dirname "$0")/.." && pwd)"
TAG="${1:-$(git -C "$SRC" rev-parse --short HEAD)}"
PROJECT="${PROJECT:-project-d24a71e9-2daf-4b2d-917}"
CTX="$(mktemp -d)"; trap 'rm -rf "$CTX"' EXIT
git -C "$SRC" ls-files -z -- apps apps2 packages packages2 pkg server-go go.mod go.sum Makefile mk var/zombie ops/docker | (cd "$SRC" && xargs -0 -r tar cf - 2>/dev/null) | tar xf - -C "$CTX"
cp "$SRC/ops/docker/shankpit.Dockerfile" "$CTX/Dockerfile"
# --suppress-logs: the CI service account (github-ci, see EMILY/gitops/CI_SETUP.md) is
# deliberately scoped to cloudbuild.builds.editor/artifactregistry.writer, not a project
# Viewer/Owner primitive role -- found live (2026-10-05): gcloud builds submit's own log-
# streaming step hard-requires one of those primitive roles to tail the default (Google-
# managed, outside-the-project) logs bucket, and fails the whole command if it can't, even
# though the actual Cloud Build job underneath already succeeded (confirmed via gcloud builds
# describe on a run that hit exactly this). Suppressing the stream sidesteps that check
# entirely rather than widening the CI SA to a primitive role just to tail output.
gcloud builds submit "$CTX" --project "$PROJECT" --tag "us-central1-docker.pkg.dev/$PROJECT/emily/shankpit:$TAG" --suppress-logs
echo "shankpit:$TAG"
