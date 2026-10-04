#!/usr/bin/env bash
# build-image.sh [TAG] — Cloud Build the shankpit (server + emily-bot) image from tracked files.
set -euo pipefail
SRC="$(cd "$(dirname "$0")/.." && pwd)"
TAG="${1:-$(git -C "$SRC" rev-parse --short HEAD)}"
PROJECT="${PROJECT:-project-d24a71e9-2daf-4b2d-917}"
CTX="$(mktemp -d)"; trap 'rm -rf "$CTX"' EXIT
git -C "$SRC" ls-files -z -- apps apps2 packages packages2 pkg server-go go.mod go.sum Makefile mk var/zombie ops/docker | (cd "$SRC" && xargs -0 -r tar cf - 2>/dev/null) | tar xf - -C "$CTX"
cp "$SRC/ops/docker/shankpit.Dockerfile" "$CTX/Dockerfile"
gcloud builds submit "$CTX" --project "$PROJECT" --tag "us-central1-docker.pkg.dev/$PROJECT/emily/shankpit:$TAG"
echo "shankpit:$TAG"
