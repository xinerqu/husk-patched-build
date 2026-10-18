#!/bin/bash
# Wait for the newest run of the test build on this repo and say how it went.
#   watch.sh <run-id> [max-minutes]
set -u
REPO=xinerqu/husk-patched-build
ID="${1:?run id}"
MAX="${2:-300}"
START=$(date +%s)
LAST=""
for _ in $(seq 1 $((MAX * 2))); do
    line=$(gh run view "$ID" --repo "$REPO" --json status,conclusion,createdAt,updatedAt \
             --jq '"\(.status) \(.conclusion // "-")"' 2>/dev/null)
    [ "$line" != "$LAST" ] && { echo "$(date +%H:%M:%S) $line"; LAST="$line"; }
    case "$line" in
        completed*) break ;;
    esac
    if [ $(( $(date +%s) - START )) -gt $((MAX * 60)) ]; then echo "still running after ${MAX} min; stopping the watch"; exit 2; fi
    sleep 30
done
echo "---- steps ----"
gh run view "$ID" --repo "$REPO" 2>&1 | sed -n '/JOBS/,/ANNOTATIONS/p' | head -40
case "$LAST" in
    *success*)  gh release list --repo "$REPO" --limit 1 ;;
    *)          echo "---- failing step's log ----"; gh run view "$ID" --repo "$REPO" --log-failed 2>&1 | tail -40 ;;
esac
