# Changelog

All releases are preserved. Version format: v1.NNN — single increment per release.

---

## v1.112 — 2026-05-16

**PtolC — checkpoint default path fix + terminology cleanup**

### Binary

- `find_checkpoint()` — fixed null-termination loop bug: loop exited on
  first NULL candidate (flag_path) before reaching `~/.ptolemy/monad_wordnet.bin`;
  `ptolemy -s` without `-c` now loads the default checkpoint correctly
- `print_version()` / `print_usage()` — "H_hat_RB Field Engine" →
  "RedBlue Geometries Engine" (terminology propagated to binary output)

### Python

- No change from v2.1.0.

### Project

- `PtolC/TODO` — GNU-standard open work items file added; post-commit hook
  auto-syncs TODO to `michaelrendier/SMMIP` on every commit that touches it
- Checkpoint milestone: Documents ingest complete — 23,895 vocab,
  9,600,426 A-edges, 34,494,302 words; baseline `monad_wordnet.bin` 148 MB

---

## v1.111 — 2026-05-16

**PtolC — C binary feature-complete**

First release under the versioning protocol. The C monad (PtolC) is now the
primary implementation of the RedBlue Geometries Engine, mirroring Philadelphos/monad.py
exactly in its mathematics while adding filesystem ingest, daemon mode, and
per-filetype token filtering.

### Binary

- `filter.c / filter.h` — learn-time token filter with per-filetype FTRules dispatch
  (prose/code/markup/doc); rejection counted in `monad.rejected_count`, never fatal
- `monad_learn_ex()` — extended learn with explicit NSFiletype; `monad_learn()` is
  now a wrapper delegating to prose rules
- `monad_health()` — reports rejected token count alongside β distribution,
  field entropy, pollution indicators, and top A-edges
- `checkpoint v2` — VocabEntry carries `home_stratum` and `gen_stratum` (NS_SIGMA_*);
  v1 checkpoints load cleanly with stratum defaulting to σ₁
- `ingest.c` — filesystem walker with extension whitelist, PRUNE_NAMES (including
  all credential/key directories), `.ptolemyignore` per-directory patterns,
  extractor dispatch (pdftotext/catdoc/pandoc/libxml2), periodic checkpoint save
- `daemon.c` — Unix domain socket daemon, HEAR/STATUS/HEALTH/QUIT protocol,
  systemd socket activation via `$LISTEN_FDS` (no libsystemd dependency)
- `log.c` — 4-hour rotating log slots, 30-day GC on Sunday 10:00
- `CMakeLists.txt` — full CMake build with GNUInstallDirs, optional libxml2/poppler,
  man page gzip, systemd unit install
- PEM guard in `monad_learn()` — refuses `-----BEGIN ...` material regardless
  of ingestion path
- Man pages: `ptolemy.1`, `ptolemyignore.5`
- Systemd user units: `ptolemy.socket`, `ptolemy.service`

### Flags added since v1.0

`-I <path>` (ingest), `-d` (daemon), `-D <query>` (daemon query),
`-F` (field health), `-S <sock>` (socket override), `-q` (quiet)

### Checkpoint

Baseline: WordNet 3.1 — 14,165 vocab entries, 766,119 A-edges, 13 MB.
Canonical home: `~/.ptolemy/monad_wordnet.bin`.

---

## v1.0 — 2026-05-16 (pre-protocol)

Initial public release. SMMIP v1.0 tag on Ptolemy3/SMMIP repos.

- `ptolemy` binary: learn (-l), hear/speak (-h), status (-s), word lookup (-w), verbosity (-v/-vv/-vvv)
- Checkpoint v1: binary format, 25,000 Riemann zeros, φ-based word addressing
- `make corpus`: WordNet ingestion via NLTK + dump_wordnet.py
- `tools/checkpoint_expand.c`: grow N in batches of 512
- `tools/ingest_system.py`: resumable Python filesystem ingest (superseded by -I flag)
- Packages: `ptolemy-1.0-linux-x86_64.tar.gz`, `ptolemy-1.0-src.tar.gz`
