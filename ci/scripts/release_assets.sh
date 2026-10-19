#!/usr/bin/env bash
# Turn this run's IPA into a Release asset, so it can be downloaded without a GitHub login.
#
# Two files, under names that do not change: Husk-latest.ipa for the sideloaders (SideStore / AltStore re-sign it) and
# Husk-latest.tipa for TrollStore (ad hoc signed with the app's own entitlements, as scripts/package_ipa.sh's --trollstore does it).
set -euo pipefail
SHA="${GITHUB_SHA:0:7}"
TAG="build-${GITHUB_RUN_NUMBER}"
IPA="${1:?ipa}"
WORK="$(mktemp -d)"

cp "$IPA" "$WORK/Husk-latest.ipa"
[[ -f "${IPA%.ipa}.tipa" ]] && cp "${IPA%.ipa}.tipa" "$WORK/Husk-latest.tipa"

{
    echo "Husk, built from \`${GITHUB_SHA:0:7}\` on $(date -u '+%Y-%m-%d %H:%M UTC')."
    echo
    echo "- **Husk-latest.ipa** — unsigned IPA (SideStore / AltStore / Sideloadly re-sign it)"
    [[ -f "$WORK/Husk-latest.tipa" ]] && echo "- **Husk-latest.tipa** — TrollStore"
    echo
    echo "Patch under test: Godot 4.7's directory and file handler answers, registered for the objects the engine actually holds (#140)."
} > "$WORK/notes.md"

FILES=("$WORK/Husk-latest.ipa")
[[ -f "$WORK/Husk-latest.tipa" ]] && FILES+=("$WORK/Husk-latest.tipa")
GH_TOKEN="${GITHUB_TOKEN:-}" gh release create "$TAG" \
    "${FILES[@]}" \
    --repo "$GITHUB_REPOSITORY" --title "Husk build $GITHUB_RUN_NUMBER ($SHA)" \
    --notes-file "$WORK/notes.md" --latest
echo "release: https://github.com/$GITHUB_REPOSITORY/releases/tag/$TAG"
