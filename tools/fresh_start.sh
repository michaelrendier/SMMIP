#!/usr/bin/env bash
# fresh_start.sh — archive current monad_wordnet.bin and reset to clean baseline
#
# Protocol:
#   1. Archive ~/.ptolemy/monad_wordnet.bin with a timestamp slug
#   2. Run eval_checkpoint.py → write assessment JSON to SMMIP checkpoints/
#   3. Append entry to checkpoints/README.md
#   4. Commit assessment + README to SMMIP with assessment summary as message
#   5. Remove monad_wordnet.bin from ~/.ptolemy/
#   6. Copy monad_English.bin → monad_wordnet.bin
#
# Usage: fresh_start.sh [label]
#   label  optional slug appended to archive name (default: "archive")

set -euo pipefail

LABEL="${1:-archive}"
STAMP=$(date +%Y%m%d_%H%M%S)
SLUG="monad_wordnet_${STAMP}_${LABEL}"

PTOLEMY_DIR="${HOME}/.ptolemy"
SMMIP_DIR="${SMMIP_REPO:-${HOME}/Projects/Ptol/SMMIP}"
EVAL_SCRIPT="${SMMIP_DIR}/tools/eval_checkpoint.py"
CKPT_DIR="${SMMIP_DIR}/checkpoints"
README="${CKPT_DIR}/README.md"

WORDNET_BIN="${PTOLEMY_DIR}/monad_wordnet.bin"
ENGLISH_BIN="${PTOLEMY_DIR}/monad_English.bin"
ARCHIVE_BIN="${PTOLEMY_DIR}/${SLUG}.bin"
ASSESSMENT_JSON="${CKPT_DIR}/${SLUG}.assessment.json"

# ── Preflight ──────────────────────────────────────────────────────────────────

if [ ! -f "${ENGLISH_BIN}" ]; then
    echo "ERROR: ${ENGLISH_BIN} not found — baseline missing, cannot reset." >&2
    echo "Restore from: gh release download v1.113 -p 'monad_English.bin' --repo michaelrendier/SMMIP" >&2
    exit 1
fi

if [ ! -f "${WORDNET_BIN}" ]; then
    echo "[fresh_start] no monad_wordnet.bin found — installing clean baseline only."
    cp "${ENGLISH_BIN}" "${WORDNET_BIN}"
    echo "Ready: monad_wordnet.bin ← monad_English.bin"
    exit 0
fi

# ── Step 1: Archive ────────────────────────────────────────────────────────────

echo "[1/5] archiving monad_wordnet.bin → ${ARCHIVE_BIN}"
cp "${WORDNET_BIN}" "${ARCHIVE_BIN}"

# ── Step 2: Assessment ────────────────────────────────────────────────────────

echo "[2/5] running assessment..."
ASSESSMENT_EXITCODE=0
if [ ! -f "${EVAL_SCRIPT}" ]; then
    echo "      WARNING: ${EVAL_SCRIPT} not found — skipping assessment."
    ASSESSMENT_EXITCODE=1
else
    python3 "${EVAL_SCRIPT}" "${ARCHIVE_BIN}" --out "${ASSESSMENT_JSON}" || ASSESSMENT_EXITCODE=$?
fi

# ── Step 3: README entry ──────────────────────────────────────────────────────

echo "[3/5] updating checkpoints/README.md..."

if [ -f "${ASSESSMENT_JSON}" ]; then
    read -r VOCAB A_EDGES WORD_COUNT SIZE_MB ENTROPY_H ENTROPY_MAX ENTROPY_PCT \
             CLEAN_COUNT POLLUTED_COUNT POLLUTION_PCT VERDICT \
             DEEPEST_WORD < <(python3 - <<'PYEOF'
import json, sys
d = json.load(open(sys.argv[1]))
top = sorted(d['top_beta'], key=lambda e: e['beta'], reverse=True)
deepest = top[0]['word'] if top else '?'
print(
    d['vocab_size'],
    d['A_size'],
    d['word_count'],
    d['size_mb'],
    d['entropy_H'],
    d['entropy_H_max'],
    d['entropy_pct'],
    d['clean_count'],
    d['polluted_count'],
    d['pollution_pct'],
    d['verdict'],
    deepest,
)
PYEOF
    "${ASSESSMENT_JSON}")

    DATE_STR=$(date +%Y-%m-%d)

    # Build the new README block
    NEW_BLOCK="
---

## ${SLUG} — ${DATE_STR}

**Assessment:** [${SLUG}.assessment.json](${SLUG}.assessment.json)
**Label:** ${VERDICT} — archived before fresh-start reset

| Metric | Value |
|--------|-------|
| Size | ${SIZE_MB} MB |
| Vocab | $(printf "%'d" ${VOCAB}) |
| A-edges | $(printf "%'d" ${A_EDGES}) |
| Words ingested | $(printf "%'d" ${WORD_COUNT}) |
| Entropy | ${ENTROPY_H} / ${ENTROPY_MAX} bits (${ENTROPY_PCT}%) |
| Clean tokens | $(printf "%'d" ${CLEAN_COUNT}) ($(echo "scale=1; (${CLEAN_COUNT}*100)/(${CLEAN_COUNT}+${POLLUTED_COUNT})" | bc)%) |
| Polluted tokens | $(printf "%'d" ${POLLUTED_COUNT}) (${POLLUTION_PCT}%) |
| Verdict | **${VERDICT}** |
| Deepest word | **${DEEPEST_WORD}** |
"

    printf '%s\n' "${NEW_BLOCK}" >> "${README}"

    COMMIT_MSG="archive ${SLUG}: ${SIZE_MB}MB vocab=${VOCAB} A-edges=${A_EDGES} entropy=${ENTROPY_H}/${ENTROPY_MAX}bits pollution=${POLLUTION_PCT}% — ${VERDICT}"
else
    ARCHIVE_SIZE=$(du -h "${ARCHIVE_BIN}" | cut -f1)
    COMMIT_MSG="archive ${SLUG}: ${ARCHIVE_SIZE} (assessment unavailable)"
fi

# ── Step 4: Commit to SMMIP ───────────────────────────────────────────────────

echo "[4/5] committing to SMMIP..."
cd "${SMMIP_DIR}"
git add checkpoints/ 2>/dev/null || true
if git diff --cached --quiet; then
    echo "      nothing staged — skipping commit."
else
    git commit -m "${COMMIT_MSG}"
    git push origin main
    echo "      pushed: ${COMMIT_MSG}"
fi

# ── Step 5: Reset baseline ────────────────────────────────────────────────────

echo "[5/5] resetting to clean baseline..."
rm "${WORDNET_BIN}"
cp "${ENGLISH_BIN}" "${WORDNET_BIN}"

echo ""
echo "Done. Baseline restored:"
ls -lh "${WORDNET_BIN}"
echo ""
echo "Archive kept at: ${ARCHIVE_BIN}"
echo ""
echo "To begin a fresh ingest:"
echo "  ptolemy -I ~/Documents"
