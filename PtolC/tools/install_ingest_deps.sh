#!/usr/bin/env bash
# PtolC/tools/install_ingest_deps.sh
#
# Install all system libraries and Python packages needed for full ingest.
# Safe to re-run — checks before installing.
#
# Usage:  bash tools/install_ingest_deps.sh

set -e
GRN='\033[0;32m'; YEL='\033[1;33m'; RST='\033[0m'
info() { echo -e "${GRN}[install]${RST} $*"; }
warn() { echo -e "${YEL}[warn]${RST}  $*"; }

# ── System packages ────────────────────────────────────────────────────────────
info "Installing system libraries..."
sudo apt install -y \
    poppler-utils \
    html2text \
    catdoc \
    unrtf \
    odt2txt \
    libmagic1 \
    python3-pip \
    python3-dev \
    e2tools \
    android-tools-adb \
    tesseract-ocr \
    tesseract-ocr-all \
    libpoppler-cpp-dev \
    2>/dev/null || warn "Some apt packages failed — continuing"

# ── Python packages ────────────────────────────────────────────────────────────
info "Installing Python libraries..."
pip3 install --quiet --upgrade \
    pdfminer.six \
    python-magic \
    python-docx \
    chardet \
    ebooklib \
    odfpy \
    striprtf \
    Pillow \
    beautifulsoup4 \
    lxml \
    2>/dev/null || warn "Some pip packages failed — continuing"

# Optional: pytesseract for OCR on image-only PDFs and scanned books
pip3 install --quiet pytesseract 2>/dev/null \
    && info "pytesseract installed (OCR available)" \
    || warn "pytesseract not installed — image-only PDFs will be skipped"

# ── Verify key tools ───────────────────────────────────────────────────────────
echo ""
info "Verification:"
for cmd in pdftotext html2text catdoc unrtf adb; do
    if command -v "$cmd" &>/dev/null; then
        echo "  ✓  $cmd"
    else
        echo "  ✗  $cmd  (not available)"
    fi
done

python3 -c "
checks = [
    ('pdfminer.high_level', 'pdfminer.six'),
    ('magic',               'python-magic'),
    ('docx',                'python-docx'),
    ('chardet',             'chardet'),
    ('ebooklib',            'ebooklib'),
    ('bs4',                 'beautifulsoup4'),
    ('striprtf.striprtf',   'striprtf'),
]
for mod, pkg in checks:
    try:
        __import__(mod)
        print(f'  ✓  {pkg}')
    except ImportError:
        print(f'  ✗  {pkg}  (pip3 install {pkg})')
"

echo ""
info "Done. Run the ingest:"
echo "  python3 tools/ingest_system.py --root / --first /SystemTree.txt"
