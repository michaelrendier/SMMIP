# PtolC Corpus Inventory

Baseline ingest corpus for benchmark reproducibility. All tests on this machine.

**Hardware:** Intel Core i7-6600U @ 2.60 GHz · 4 logical cores · 8 GB RAM · Ubuntu/Linux

**Binary:** PtolC v1.115+ · N=25,000 Riemann zeros · checkpoint format v3

**Ingest order:**
```
make corpus          # WordNet 3.1 via NLTK (-L flag → prose_seen=2)
make grammar         # English function-word lexicon (~1,800 words)
./ptolemy -I ~/Documents
./ptolemy -I ~/Desktop/Desktop\ Archive/thesearecool
```

---

## Field state after full ingest

| Metric | Value |
|--------|-------|
| Vocab (occupied zeros) | 24,485 / 25,000 |
| A-edges | 6,825,748 |
| Words processed | 121,914,388 |
| Deepest β | 7.5520 (z#0, "seemy") |
| Ground state | 0.000076 |

---

## Corpus 1 — WordNet 3.1

| Field | Value |
|-------|-------|
| Source | NLTK `wordnet` corpus |
| Ingest flag | `-L` (NS_FT_WORDNET) |
| prose_seen | 2 (canonical dictionary) |
| Vocab contributed | ~14,200 words |
| A-edges contributed | 6,825,748 |
| Words processed | ~46M tokens from synset definitions |

NLTK WordNet 3.1: 117,798 lemmas across 82,115 synsets. Definitions and examples
are tokenized and ingested as continuous prose. The `-L` flag marks all seated words
as canonical English dictionary entries (`prose_seen=2`).

---

## Corpus 2 — ~/Documents

**21,476 ingested files** · primarily an archived book collection in HTML + PDF

| Extension | Count | Filetype | Notes |
|-----------|-------|----------|-------|
| `.htm` | 20,866 | markup | Online book archive (flippingbook, htm reader exports) |
| `.pdf` | 334 | doc | Academic papers, manuals, religious texts, books |
| `.txt` | 213 | prose | Notes, transcripts, plain-text books |
| `.doc` | 22 | doc | Legacy Word documents |
| `.js` | 16 | code | Reader app scripts (filtered as code) |
| `.odt` | 11 | doc | OpenDocument text |
| `.html` | 10 | markup | Standalone HTML pages |
| `.rtf` | 2 | doc | Rich text |
| `.svg` | 1 | markup | Inline SVG |
| `.docx` | 1 | doc | Word document |

### Subdirectory breakdown

| Directory | Files | Content |
|-----------|-------|---------|
| `books/` | 21,390 | Book archive — metaphysics, chemistry, physics, history, esoteric |
| `Master Key of Reality/` | 28 | Personal manuscript |
| `Natal Charts/` | 20 | Astrological reference documents |
| `(root)` | 19 | Miscellaneous: Paradise Lost, AI book, VW diesel manual, phone list |
| `eBooks/` | 9 | Additional ebooks |
| `flippingbook/` | 7 | Flippingbook exports (FM 21-76 survival, Enoch, etc.) |
| `Herbs/` | 2 | Herbal reference |
| `Brandon Brown Germany/` | 1 | Travel document |

Notable root-level titles: `Paradise Lost` (Milton), `Artificial Intelligence with Python`,
`VW AAZ/1Z/AHU Diesel Service Manual`, `Improvised Munitions Handbook v3`, `Jeff Bible`.

---

## Corpus 3 — ~/Desktop/Desktop Archive/thesearecool

**1,429 ingested files** · security tools, blockchain, technical whitepapers, OSINT

| Extension | Count | Filetype | Notes |
|-----------|-------|----------|-------|
| `.pdf` | 516 | doc | Technical whitepapers, research papers |
| `.c` | 393 | code | C source — exploit code, security tools |
| `.h` | 150 | code | C headers |
| `.txt` | 118 | prose | Hosts lists, wordlists, READMEs |
| `.py` | 96 | code | Python security tools |
| `.java` | 46 | code | Java source |
| `.md` | 44 | prose | Documentation, READMEs |
| `.svg` | 15 | markup | Diagrams |
| `.sh` | 14 | code | Shell scripts |
| `.html` | 11 | markup | Tool documentation pages |
| `.rb` | 8 | code | Ruby tools |
| `.js` | 8 | code | JavaScript |
| `.doc` | 7 | doc | Legacy documents |
| `.xml` | 2 | markup | Configuration |
| `.cpp` | 1 | code | C++ source |

### Project breakdown

| Project | Files | Content |
|---------|-------|---------|
| `cyberweapons-master/` | 653 | Assembled cyberweapons archive (C, headers, docs) |
| `Blockchain-master/` | 357 | Blockchain research — whitepapers, Java source |
| `technical-whitepapers-master/` | 154 | Security and networking whitepapers (PDF) |
| `venom-master/` | 92 | VENOM security framework (Python, shell) |
| `alfred-main/` | 42 | Alfred OSINT tool (Python) |
| `1Hosts-master/` | 36 | DNS hosts blocklist collection |
| `DaProfiler-main/` | 31 | OSINT profiler tool |
| `blackweb-master/` | 17 | Blackweb hosts blocklist |
| `Crypto Info/` | 14 | Cryptocurrency reference PDFs |
| `additional-hosts-master/` | 12 | Supplemental DNS blocklists |
| `Badd-Boyz-Hosts-master/` | 10 | Additional hosts lists |
| `FireFly-main/` | 3 | FireFly tool |
| `ADios-master/` | 2 | ADios tool |
| `no-infoga.py-main/` | 2 | infoga replacement |
| `querytool-master/` | 2 | Query tool |
| `DevilX-main/` | 1 | DevilX |
| `osint-tools-master/` | 1 | OSINT aggregator |

---

## Token filter summary

Tokens are filtered per-filetype before zero assignment. Universal rejections:
length < 2, pure numeric, UUID, `key=value`, `://` fragments.

| Filetype | Max len | Require vowel | base64 | Hex | Slash |
|----------|---------|---------------|--------|-----|-------|
| prose | 24 | yes | ≥16 chars | no | no |
| code | 40 | no | — | no | no |
| markup | 24 | yes | ≥16 chars | no | no |
| doc | 24 | yes | ≥16 chars | no | no |
| wordnet | 24 | yes | ≥16 chars | no | no |

Words from WordNet (`prose_seen=2`) that also appear in real prose get upgraded to
`prose_seen=3` — the surface translation layer prefers these as canonical common English.

---

## Reproducibility

```bash
# Reproduce this baseline on any machine with pdftotext + catdoc + pandoc + libxml2:
git clone https://github.com/michaelrendier/Ptolemy ~/Ptolemy
cd ~/Ptolemy/PtolC
make
make corpus
make grammar
./ptolemy -I ~/Documents
./ptolemy -I /path/to/thesearecool
```

The ingest is resumable: `-I` saves checkpoint every 500 files.
Word addressing is deterministic (bijective Horner + φ seed), so
re-ingesting the same corpus always produces the same zero assignments.
