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
if [ -f "${IPA%.ipa}.tipa" ]; then
    cp "${IPA%.ipa}.tipa" "$WORK/Husk.tipa"
elif [ -f "$IPA.tipa" ]; then
    cp "$IPA.tipa" "$WORK/Husk.tipa"
else
    echo "no .tipa from package_ipa.sh; using the .ipa for TrollStore too"
    cp "$IPA" "$WORK/Husk.tipa"
fi

# Zip the .ipa's Payload so each archive is one file the installers can take directly.
( cd "$WORK" && mv "Husk-$SHA.ipa" Husk.ipa && zip -qry -0 "Husk-$SHA.zip" Husk.ipa && mv Husk.ipa "Husk-$SHA.ipa" )
cp "$WORK/Husk-$SHA.zip" "$WORK/Husk-latest.zip"
cp "$WORK/Husk.tipa" "$WORK/Husk-latest.tipa"

{
    echo "Husk, built from \`${GITHUB_SHA:0:7}\` on $(date -u '+%Y-%m-%d %H:%M UTC')."
    echo
    echo "- **Husk-$SHA.zip** — unsigned IPA (SideStore / AltStore / Sideloadly re-sign it)"
    echo "- **Husk-$SHA.tipa** — TrollStore (ad hoc signed, Husk's entitlements kept)"
    echo "- Husk-latest.zip / Husk-latest.tipa — the same files, always the newest build"
    echo
    echo "Built on a GitHub runner: the PS4 layer is a stand-in (its real build needs private trees), Android apps through the translation layer are complete."
} > "$WORK/notes.md"

GH_TOKEN="${GITHUB_TOKEN:-}" gh release create "$TAG" \
    "$WORK/Husk-$SHA.zip" "$WORK/Husk.tipa" "$WORK/Husk-latest.zip" "$WORK/Husk-latest.tipa" \
    --repo "$GITHUB_REPOSITORY" --title "Husk build $GITHUB_RUN_NUMBER ($SHA)" \
    --notes-file "$WORK/notes.md" --latest
echo "release: https://github.com/$GITHUB_REPOSITORY/releases/tag/$TAG"
