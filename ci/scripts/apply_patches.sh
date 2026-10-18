#!/usr/bin/env bash
# Paste in the patch under test and check it applies; the build then compiles the patched sources.
set -euo pipefail
shopt -s nullglob
here="$(cd "$(dirname "$0")/.." && pwd)"          # the checkout
patches=("$here"/ci/patches/*.patch)
if [ ${#patches[@]} -eq 0 ]; then echo "==> no ci/patches/*.patch to apply"; exit 0; fi
for p in "${patches[@]}"; do
    echo "==> applying $(basename "$p")"
    git apply --check "$p" || { echo "patch does not apply to this source; refusing to build" >&2; exit 1; }
    git apply "$p"
done
git --no-pager diff --stat
