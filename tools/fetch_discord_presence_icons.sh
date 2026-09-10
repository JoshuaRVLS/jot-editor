#!/usr/bin/env bash
# Fetches the Discord Rich Presence asset set (one PNG per asset key) from
# iCrawl/discord-vscode at a pinned commit.
#
# Why the assets live in-tree: Discord Rich Presence accepts only *asset keys*
# over IPC -- the artwork itself must be uploaded to the Discord application
# behind the client id first (Developer Portal -> your app -> Rich Presence ->
# Art Assets). Keeping the exact key set next to the upload instructions
# (ASSETS.md) makes that a copy-paste job instead of a hunt through the
# upstream repository.
#
# Upstream is MIT licensed; the notice is vendored beside the icons.
#
# Usage: tools/fetch_discord_presence_icons.sh [destination]
#   destination default: packaging/discord-presence/icons
set -euo pipefail

COMMIT="6d5fb18969e1dba1e64f86d7fb758896aed1f1b3"
BASE="https://raw.githubusercontent.com/iCrawl/discord-vscode/${COMMIT}/assets"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${1:-$ROOT/packaging/discord-presence/icons}"

mkdir -p "$DEST"

echo "Fetching icon list from ${BASE}/icons ..."
# The tree listing gives the authoritative file set; parse the icon paths out
# of it so the script never drifts from upstream.
ICONS="$(
  curl -fsSL "https://api.github.com/repos/iCrawl/discord-vscode/git/trees/${COMMIT}?recursive=1" \
    | grep -o '"path": "assets/icons/[^"]*\.png"' \
    | sed 's/"path": "assets\/icons\///; s/"$//' \
    | sort
)"
COUNT="$(printf '%s\n' "$ICONS" | grep -c . || true)"
echo "Found ${COUNT} icons."

# logo.png is the extension's marketplace icon, not a presence asset; it is
# fetched separately below and stored outside icons/.
FAILED=0
while IFS= read -r name; do
  [ -n "$name" ] || continue
  if [ -s "$DEST/$name" ]; then
    continue
  fi
  if ! curl -fsSL --retry 3 --retry-delay 1 -o "$DEST/$name" "${BASE}/icons/${name}"; then
    echo "FAILED: $name" >&2
    FAILED=$((FAILED + 1))
  fi
done <<< "$ICONS"

echo "Icons in $DEST: $(find "$DEST" -name '*.png' | wc -l)"
if [ "$FAILED" -ne 0 ]; then
  echo "$FAILED icon(s) failed to download" >&2
  exit 1
fi

echo "Done. See packaging/discord-presence/ASSETS.md for the upload steps."
