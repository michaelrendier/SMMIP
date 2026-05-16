#!/usr/bin/env bash
# tools/push_bin.sh — push current monad_wordnet.bin to SMMIP repo
#
# Run after every git push to any repo:
#   bash tools/push_bin.sh
#
# The SMMIP repo is the canonical shared field checkpoint.
# Version history = field progression. Every push makes it richer.

set -e

BIN="$(dirname "$0")/../monad_wordnet.bin"
SMMIP="${SMMIP_REPO:-/tmp/smmip_work}"

if [ ! -f "$BIN" ]; then
    echo "ERROR: monad_wordnet.bin not found at $BIN"
    exit 1
fi

if [ ! -d "$SMMIP/.git" ]; then
    echo "ERROR: SMMIP repo not found at $SMMIP"
    echo "Set SMMIP_REPO=/path/to/SMMIP or clone it there."
    exit 1
fi

SIZE=$(du -sh "$BIN" | cut -f1)
echo "[push_bin] $BIN  ($SIZE) → $SMMIP"

cp "$BIN" "$SMMIP/monad_wordnet.bin"

cd "$SMMIP"
git add monad_wordnet.bin
if git diff --cached --quiet; then
    echo "[push_bin] no change in .bin — nothing to push"
    exit 0
fi

STAMP=$(date '+%Y-%m-%d %H:%M')
git commit -m "field: monad_wordnet.bin update $STAMP"
git push origin main
echo "[push_bin] done"
