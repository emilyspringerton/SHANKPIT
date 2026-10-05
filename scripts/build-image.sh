#!/usr/bin/env bash
# build-image.sh [TAG] — Cloud Build the shankpit (server + emily-bot) image from tracked files.
set -euo pipefail
SRC="$(cd "$(dirname "$0")/.." && pwd)"
TAG="${1:-$(git -C "$SRC" rev-parse --short HEAD)}"
PROJECT="${PROJECT:-project-d24a71e9-2daf-4b2d-917}"
CTX="$(mktemp -d)"; trap 'rm -rf "$CTX"' EXIT
git -C "$SRC" ls-files -z -- apps apps2 packages packages2 pkg server-go go.mod go.sum Makefile mk var/zombie ops/docker | (cd "$SRC" && xargs -0 -r tar cf - 2>/dev/null) | tar xf - -C "$CTX"
cp "$SRC/ops/docker/shankpit.Dockerfile" "$CTX/Dockerfile"

# Found live (2026-10-05), two distinct gaps in the documented CI setup (EMILY/gitops/CI_SETUP.md,
# needs updating) beyond the base artifactregistry.writer/cloudbuild.builds.editor roles:
#
# 1. --gcs-source-staging-dir: without it, `builds submit` auto-detects the staging bucket via a
#    PROJECT-SCOPED `storage.buckets.list` call (list buckets filtered by prefix), which the CI SA's
#    bucket-level bindings (objectAdmin/legacyBucketReader/legacyBucketWriter, scoped to just the
#    one bucket) do NOT satisfy -- confirmed via --verbosity=debug, the failing call is
#    `GET /storage/v1/b?project=...&prefix=...`, a 403, surfaced as a misleading "forbidden ...
#    serviceusage.services.use" error. Pinning the staging dir skips that list call entirely.
#
# 2. --async, not --suppress-logs: the CI SA is deliberately not a project Viewer/Owner (see
#    CI_SETUP.md), so `builds submit` cannot stream the default (Google-managed, outside-the-
#    project) logs bucket -- confirmed the underlying Cloud Build job succeeds regardless. Found
#    live: --suppress-logs alone still polls/waits on the build and hits this same error (exit 1)
#    even though it doesn't print the log lines. --async skips the wait entirely, so we poll
#    `builds describe` (status only, never logs) ourselves below.
#
# Also requires roles/iam.serviceAccountUser for the CI SA on the Compute Engine default SA
# (532442865445-compute@developer.gserviceaccount.com) -- Cloud Build runs the worker as that SA
# by default, and submitting a build means "acting as" it.
BUILD_ID=$(gcloud builds submit "$CTX" --project "$PROJECT" \
  --tag "us-central1-docker.pkg.dev/$PROJECT/emily/shankpit:$TAG" \
  --gcs-source-staging-dir="gs://${PROJECT}_cloudbuild/source" \
  --async --format="value(id)")

echo "submitted build $BUILD_ID, polling for completion..."
while true; do
  STATUS=$(gcloud builds describe "$BUILD_ID" --project "$PROJECT" --format="value(status)")
  case "$STATUS" in
    SUCCESS) echo "shankpit:$TAG"; exit 0 ;;
    FAILURE|INTERNAL_ERROR|TIMEOUT|CANCELLED|EXPIRED) echo "build $BUILD_ID: $STATUS" >&2; exit 1 ;;
    *) sleep 5 ;;
  esac
done
