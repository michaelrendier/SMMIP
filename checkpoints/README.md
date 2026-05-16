# Checkpoint Archive

Checkpoint `.bin` files are too large for git (147MB+) and are distributed
via GitHub Releases. This directory holds the assessment JSON for each
checkpoint so the field history is fully auditable without the binary.

Run `tools/eval_checkpoint.py <file.bin>` to generate a new assessment.

---

## monad_v1.111_documents — 2026-05-16

**Release:** [v1.111](https://github.com/michaelrendier/SMMIP/releases/tag/v1.111)  
**Assessment:** [monad_v1.111_documents.assessment.json](monad_v1.111_documents.assessment.json)  
**Label:** POLLUTED — consonant-cluster and trailing-apostrophe noise present

| Metric | Value |
|--------|-------|
| Size | 147.26 MB |
| Vocab | 23,897 |
| A-edges | 9,601,358 |
| Words ingested | 34,753,802 |
| Entropy | 12.78 / 14.54 bits (87.9%) |
| Clean tokens | 21,343 (89.3%) |
| Polluted tokens | 2,554 (10.7%) |
| Verdict | **PASS** (< 20% threshold) |

**Corpus:** WordNet 3.1 + ~/Documents (19,131 files, 2.1 GB, 71m30s)  
**Known pollution sources:**
- Consonant-only clusters (1,939) — OCR artifacts from PDFs (VW diesel manual, TRADOC FM21-76, survival manual), technical abbreviations
- Consonant-cluster-3 (1,144) — same sources, 3-char abbreviation codes
- No-vowel (862) — overlap with above
- Trailing apostrophes (275) — archaic/biblical texts (KJV × 4 versions, sacred texts archive); tokenizer keeps trailing `'`
- Apostrophe fragments (202) — same
- Hex strings (98) — HTML entities, PDF metadata

**Filter gaps identified:**
- No vowel-ratio check in `token_accept()` — consonant clusters pass prose rules
- Tokenizer does not strip leading/trailing apostrophes
- See `PtolC/TODO` for remediation items

**Top A-edge anomaly:** `ban' — each` (w=3087) — "ban" appears with trailing apostrophe throughout the KJV sacred texts corpus, creating a dominant polluted A-edge. The underlying concept (prohibition) is correctly addressed; only the surface form is polluted.
