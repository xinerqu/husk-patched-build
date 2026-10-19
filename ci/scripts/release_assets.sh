#!/usr/bin/env bash
# Turn this run's IPA into a Release asset, so it can be downloaded without a GitHub login.
#
# Three files: Husk-<sha>.zip for the sideloaders (SideStore/AltStore re-sign it), Husk-<sha>.tipa for TrollStore (ad hoc signed
# with the app's own entitlements, as scripts/package_ipa.sh's --trollstore does it), and Husk-latest.zip, which always holds the
# newest one under a name that does not change.
set -euo pipefail
SHA="${GITHUB_SHA:0:7}"
TAG="build-${GITHUB_RUN_NUMBER}"
IPA="${1:?ipa}"
WORK="$(mktemp -d)"

cp "$IPA" "$WORK/Husk-$SHA.ipa"
cp "$IPA" "$WORK/Husk-latest.ipa"

{
    echo "Husk, built from \`${GITHUB_SHA:0:7}\` on $(date -u '+%Y-%m-%d %H:%M UTC')."
    echo
    echo "- **Husk-latest.ipa** — unsigned IPA, the newest build (SideStore / AltStore / Sideloadly re-sign it)"
    echo "- Husk-$SHA.ipa — the same file under its commit"
    echo
    echo "The PS4 layer is a stand-in in this build (its real build needs private trees); everything the Android translation layer runs, Godot included, is here."
} > "$WORK/notes.md"

GH_TOKEN="${GITHUB_TOKEN:-}" gh release create "$TAG" \
    "$WORK/Husk-$SHA.ipa" "$WORK/Husk-latest.ipa" \
    --repo "$GITHUB_REPOSITORY" --title "Husk build $GITHUB_RUN_NUMBER ($SHA)" \
    --notes-file "$WORK/notes.md" --latest
echo "release: https://github.com/$GITHUB_REPOSITORY/releases/tag/$TAG"
