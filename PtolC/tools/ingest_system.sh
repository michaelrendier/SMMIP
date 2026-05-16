#!/usr/bin/env bash
# PtolC/tools/ingest_system.sh — full filesystem ingest for ptolemy
#
# Recursively visits every file from ROOT, extracts text, feeds ptolemy.
# Saves checkpoint every BATCH_FILES files. Safe to kill at any time.
#
# SETUP (run once before starting):
#
#   cd PtolC
#   make
#   gcc -O2 -o tools/checkpoint_expand tools/checkpoint_expand.c -lm
#
#   # Authorize zero expansion — 450 batches of 512 = 230400 new zeros
#   ./tools/checkpoint_expand monad_wordnet.bin 255400
#   ./ptolemy -s   # verify N=255400
#
#   # Optional: install extractors
#   sudo apt install poppler-utils html2text catdoc unrtf
#   pip3 install pdfminer.six python-docx
#
# RUN OVERNIGHT:
#
#   cd PtolC
#   nohup bash tools/ingest_system.sh / 2>&1 | tee ingest.log &
#   echo "PID: $!"
#
# ARGUMENTS:
#   $1  root directory   (default: /)
#   $2  ptolemy binary   (default: ./ptolemy)
#   $3  files per batch  (default: 2000)

set -euo pipefail

ROOT="${1:-/}"
PTOLEMY="${2:-./ptolemy}"
BATCH_FILES="${3:-2000}"     # checkpoint save every N files

# ── Sanity ─────────────────────────────────────────────────────────────────────
if [ ! -x "$PTOLEMY" ]; then
    echo "ERROR: ptolemy not found at '$PTOLEMY'. Build with: cd PtolC && make"
    exit 1
fi

echo "════════════════════════════════════════════════════════════════"
echo "  ptolemy full system ingest"
echo "  root:       $ROOT"
echo "  binary:     $PTOLEMY"
echo "  batch size: $BATCH_FILES files per checkpoint save"
echo "  started:    $(date)"
echo "════════════════════════════════════════════════════════════════"
echo ""
$PTOLEMY -s 2>/dev/null || true
echo ""

# ── Directories to skip ────────────────────────────────────────────────────────
PRUNE_ARGS=(
    \( -path /proc -o -path /sys -o -path /dev -o -path /run -o -path /snap \)
    -prune -o
)

# ── Text extraction ────────────────────────────────────────────────────────────
extract_text() {
    local f="$1"
    [ -r "$f" ] || return 0

    local mime
    mime=$(file -b --mime-type "$f" 2>/dev/null) || return 0

    case "$mime" in
        text/*)
            cat "$f" 2>/dev/null
            ;;
        application/pdf)
            if command -v pdftotext &>/dev/null; then
                pdftotext -q -nopgbrk -enc UTF-8 "$f" - 2>/dev/null
            elif python3 -c "import pdfminer" 2>/dev/null; then
                python3 -c "
from pdfminer.high_level import extract_text as et
import sys
try: print(et(sys.argv[1]))
except: pass
" "$f" 2>/dev/null
            fi
            ;;
        application/gzip|application/x-gzip)
            # Decompress then check if content is text
            local inner
            inner=$(zcat "$f" 2>/dev/null | file -b --mime-type -) || return 0
            case "$inner" in
                text/*) zcat "$f" 2>/dev/null | col -b 2>/dev/null ;;
            esac
            ;;
        application/x-bzip2)
            bzcat "$f" 2>/dev/null
            ;;
        application/x-xz)
            xzcat "$f" 2>/dev/null
            ;;
        text/html|application/xhtml+xml)
            if command -v html2text &>/dev/null; then
                html2text "$f" 2>/dev/null
            else
                sed 's/<[^>]*>//g; /^[[:space:]]*$/d' "$f" 2>/dev/null
            fi
            ;;
        application/msword)
            command -v catdoc &>/dev/null && catdoc "$f" 2>/dev/null
            ;;
        application/rtf|text/rtf)
            command -v unrtf &>/dev/null && unrtf --text "$f" 2>/dev/null
            ;;
        application/epub+zip)
            python3 -c "
import sys, zipfile, re
try:
    with zipfile.ZipFile(sys.argv[1]) as z:
        for n in z.namelist():
            if n.endswith(('.xhtml','.html','.htm')):
                print(re.sub('<[^>]+>','',z.read(n).decode('utf-8','ignore')))
except: pass
" "$f" 2>/dev/null
            ;;
        application/vnd.oasis.opendocument.text)
            command -v odt2txt &>/dev/null && odt2txt --stdout "$f" 2>/dev/null
            ;;
        application/vnd.openxmlformats-officedocument.wordprocessingml.document)
            python3 -c "
import sys
try:
    import docx
    for p in docx.Document(sys.argv[1]).paragraphs: print(p.text)
except: pass
" "$f" 2>/dev/null
            ;;
        *)
            # Skip silently — binary, image, audio, etc.
            ;;
    esac
}

# ── Batch-learn with periodic checkpoint saves ─────────────────────────────────
TMPBATCH=$(mktemp /tmp/ptolemy_batch_XXXXXX.txt)
trap 'rm -f "$TMPBATCH"' EXIT

files_done=0
batches_done=0
files_in_batch=0

flush_batch() {
    if [ -s "$TMPBATCH" ]; then
        ((batches_done++)) || true
        local lc
        lc=$(wc -l < "$TMPBATCH")
        echo "[$(date +%T)] batch $batches_done: $lc lines from $files_in_batch files → learning..."
        "$PTOLEMY" -l - < "$TMPBATCH"
        echo "[$(date +%T)] batch $batches_done: checkpoint saved"
        > "$TMPBATCH"
        files_in_batch=0
    fi
}

echo "[$(date +%T)] scanning $ROOT ..."

while IFS= read -r f; do
    extract_text "$f" >> "$TMPBATCH" 2>/dev/null
    ((files_done++))  || true
    ((files_in_batch++)) || true

    if (( files_done % 200 == 0 )); then
        echo "[$(date +%T)] $files_done files visited  batches_saved=$batches_done"
    fi

    if (( files_in_batch >= BATCH_FILES )); then
        flush_batch
    fi
done < <(find "$ROOT" "${PRUNE_ARGS[@]}" -type f -print 2>/dev/null)

flush_batch   # final partial batch

echo ""
echo "════════════════════════════════════════════════════════════════"
echo "  INGEST COMPLETE"
echo "  files visited:   $files_done"
echo "  batches saved:   $batches_done"
echo "  finished:        $(date)"
echo "════════════════════════════════════════════════════════════════"
echo ""
$PTOLEMY -s 2>/dev/null || true
