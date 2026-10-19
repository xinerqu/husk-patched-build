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
# The sideloaded IPA only: a TrollStore .tipa is a second codesign pass, and this build is for one test install at a time.
FILES=("$WORK/Husk-$SHA.ipa")

# Zip the .ipa's Payload so each archive is one file the installers can take directly.
( cd "$WORK" && mv "Husk-$SHA.ipa" Husk.ipa && zip -qry -0 "Husk-$SHA.zip" Husk.ipa && mv Husk.ipa "Husk-$SHA.ipa" )
cp "$WORK/Husk-$SHA.zip" "$WORK/Husk-latest.zip"
FILES+=("$WORK/Husk-$SHA.zip" "$WORK/Husk-latest.zip")

{
    echo "Husk, built from \`${GITHUB_SHA:0:7}\` on $(date -u '+%Y-%m-%d %H:%M UTC')."
    echo
    echo "- **Husk-$SHA.zip** — unsigned IPA (SideStore / AltStore / Sideloadly re-sign it)"
    echo "- Husk-latest.zip — the same IPA, always the newest build"
    echo
    echo "Built on a GitHub runner: the PS4 layer is a stand-in (its real build needs private trees), Android apps through the translation layer are complete."
} > "$WORK/notes.md"

GH_TOKEN="${GITHUB_TOKEN:-}" gh release create "$TAG" \
    "${FILES[@]}" \
    --repo "$GITHUB_REPOSITORY" --title "Husk build $GITHUB_RUN_NUMBER ($SHA)" \
    --notes-file "$WORK/notes.md" --latest
echo "release: https://github.com/$GITHUB_REPOSITORY/releases/tag/$TAG"
