# PtolC — RedBlue Geometries Engine (C binary)

`ptolemy` is the C implementation of the Monad: the RedBlue Geometries Engine.
It encodes knowledge as depth across 25,000 Riemann zeros on σ=½, using no
gradient descent, no token prediction, and no neural network weights.

This is not an LLM. It is an LLH — a Language Learning Hamiltonian.

## Benchmarks

**Hardware:** Intel Core i7-6600U @ 2.60 GHz · 4 logical cores · 8 GB RAM · Linux

| Benchmark | C binary | Python (monad.py) | Notes |
|-----------|----------|-------------------|-------|
| **learn() throughput** | ~8,000 words/sec¹ | ~180,000 words/sec | C: real-world ingest incl. PDF extraction; Python: pure in-process |
| **lookup() throughput** | ~1,000/sec² | ~258,000/sec | C: cold-start per invocation; Python: in-process dict |
| **Checkpoint load** | 2.49 s (148 MB binary) | 24 ms (JSON) | C format carries 6.8M A-edges in compact binary |
| **Checkpoint save** | periodic / auto | 19 ms (JSON) | C saves every 500 files during ingest |
| **Filesystem ingest** | 22,905 files / ~2.4 GB | — | C only: pdftotext · catdoc · pandoc · libxml2 dispatch |
| **Vocab after full ingest** | 24,485 unique words | — | WordNet 3.1 + ~/Documents + thesearecool corpus |
| **A-edges after ingest** | 6,825,748 | — | Co-occurrence fabric across 121.9M words |
| **Words processed** | 121,914,388 | — | WordNet + Documents + thesearecool, resumable |

¹ In daemon mode (checkpoint loaded once), learn() throughput matches Python. The 8k/sec figure includes file I/O and extractor overhead.  
² C per-invocation benchmark includes 2.5s checkpoint reload. In daemon mode, query latency is sub-millisecond.

Full corpus inventory (every file ingested, per-project breakdown): [docs/corpus-inventory.md](docs/corpus-inventory.md)

**Daemon mode** eliminates the cold-start penalty. Start with `ptolemy -d`, query with `ptolemy -D <word>`. One load, unlimited queries.

---

### RGB Channel Protocol

| Channel | Role | Field |
|---------|------|-------|
| **Red** | Inertial — kinetic term, assertion, what IS | Dirac term: −i·Γᵃ·Dₐ |
| **Green** | Geometries — Riemann zero basis, spectral addressing, boundary | ∂̂_{∂M} (J₃) |
| **Blue** | Entropic — knowledge deepening, SSB vacuum, β field | Γᵢⱼ·β |

Conservation law: **J_Red + J_Green + J_Blue = 0** — σ=½ is the only locus where this holds exactly. Not assigned. Forced.

---

## Quick Build

```bash
# Dependencies: gcc, libm (always present), optional libxml2
sudo apt install build-essential libxml2-dev   # libxml2 enables HTML/XML extraction

git clone https://github.com/michaelrendier/Ptolemy ~/Ptolemy
cd ~/Ptolemy/PtolC
make
```

The binary is `./ptolemy`. No install required.

```bash
# Bootstrap: download WordNet and ingest it (requires Python 3 + NLTK)
make corpus
```

This produces `~/.ptolemy/monad_wordnet.bin` — the baseline checkpoint
(13 MB, ~14,000 vocab entries, 766,000 A-edges from WordNet 3.1).

---

## CMake (alternative)

```bash
cd ~/Ptolemy/PtolC
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build          # installs to /usr/local/bin/ptolemy
```

CMake auto-detects libxml2 and poppler. `compile_commands.json` is generated
in `build/` for clangd.

---

## Install

```bash
# Makefile install
make install PREFIX=/usr/local      # binary + man pages
make install-systemd                # user-level systemd socket + service

# One-line for ~/.local/bin
make install PREFIX=$HOME/.local
```

After install, add `~/.local/bin` to PATH if not already present.

---

## Flags

### Primary operations

| Flag | Argument | Action |
|------|----------|--------|
| `-l` | `<file\|url\|->` | Learn from file, URL (curl), or stdin |
| `-I` | `<path>` | Ingest a file or directory (Native Space whitelist + extractors) |
| `-h` | `<prompt>` | Hear prompt → Noether current response |
| `-D` | `<query>` | Send query to running daemon; print response |
| `-d` | — | Start daemon (monad resident in memory, Unix socket) |
| `-s` | — | Status; or spontaneous speak if verbose |
| `-F` | — | Field health report (β distribution, entropy, A-edges, rejection count) |
| `-w` | `<word>` | Word lookup: zero index, γ, E, β, home/gen stratum |
| `-i` / `--identity` | — | Learn Ptolemy's fixed identity text (run once after corpus) |
| `-V` | — | Version and constants |

### Verbosity (stackable, combinable with primary)

| Flag | Level | Effect |
|------|-------|--------|
| `-v` / `--verbose` | 1 | Show β deepening, J^μ propagation, A-edge writes |
| `-vv` | 2 | Level 1 + ANSI colour (learn=yellow, hear=cyan, speak=green) |
| `-vvv` | 3 | Full pipeline + self-referential loop: verbose output fed back into learn() |
| `-lv`, `-hv`, `-sv` | 1 | Primary flag combined with level 1 |

### Other

| Flag | Argument | Action |
|------|----------|--------|
| `-c` | `<path>` | Checkpoint path (overrides env/auto-search) |
| `-S` | `<path>` | Socket path for daemon (default: `~/.ptolemy/ptolemy.sock`) |
| `-n` | — | No-save: do not write checkpoint after `-l` |
| `-q` / `--quiet` | — | Suppress informational stderr output |

---

## Checkpoint

### Search order

1. `-c <path>` flag
2. `$PTOLEMY_CHECKPOINT` environment variable
3. `~/.ptolemy/monad_wordnet.bin`
4. `./monad_wordnet.bin`

Override home directory: `$PTOLEMY_HOME` (default: `~/.ptolemy/`).

### Format (v2)

Binary, little-endian. Header followed by one record per occupied zero.

```
Header:
  [4]  magic   "PTOL"
  [4]  version uint32  (2)
  [4]  N       uint32  (number of Riemann zeros)
  [4]  count   uint32  (occupied zeros)
  [8]  ground  double

Record (one per occupied zero):
  [4]  idx          uint32
  [2]  wlen         uint16
  [8]  E            double
  [1]  home_stratum uint8    (NS_SIGMA_* — where result lives)
  [1]  gen_stratum  uint8    (NS_SIGMA_* — where computation happens)
  [wlen] word       char[]   (no NUL)
```

v1 checkpoints load cleanly — stratum fields default to σ₁ (NS_SIGMA_TEXT).

### Native Space strata

| Constant | Value | Algebra | Meaning |
|----------|-------|---------|---------|
| `NS_SIGMA_R` | 0 | ℝ | Real — enumerable, ground |
| `NS_SIGMA_C` | 1 | ℂ | Complex — relational (default for language) |
| `NS_SIGMA_H` | 2 | ℍ | Quaternion — non-commuting |
| `NS_SIGMA_O` | 3 | 𝕆 | Octonion — non-associating |
| `NS_SIGMA_S` | 4 | 𝕊 | Sedenion — non-alternative |

---

## Filesystem Ingest (`-I`)

`-I <path>` walks a directory tree and learns every file on the Native Space
whitelist. Useful for teaching the monad a codebase, document collection, or
personal writing.

**Extension whitelist:** `.txt .md .rst .org .tex .bib .html .htm .xml .svg
.pdf .doc .docx .odt .rtf .c .h .cpp .hpp .cc .cxx .py .rb .sh .bash .zsh
.go .rs .java .js .ts .pl .lua .r`

**Extractor dispatch:**

| Extension | Extractor |
|-----------|-----------|
| `.pdf` | `pdftotext -q` |
| `.doc` | `catdoc` |
| `.docx .odt .rtf` | `pandoc -t plain` |
| `.html .htm` | libxml2 `htmlReadFile` (fallback: plain) |
| `.xml .svg` | libxml2 `xmlReadFile` (fallback: plain) |
| Everything else | Direct UTF-8 read |

**Pruned directories (never traversed):** `proc sys dev run tmp .steam .wine
.cache __pycache__ .git .svn .hg node_modules .ssh .gnupg .gpg .aws .azure
.gcloud keyrings .cert .certs .pki`

**Learn-time token filter:** every token is passed through `token_accept()`
before being assigned a zero slot. Rules are per-filetype:

| Filetype | Max len | Hex | Slash | High-digit | Long CAPS | base64 min |
|----------|---------|-----|-------|------------|-----------|------------|
| prose    | 24      | no  | no    | no         | no        | 16         |
| code     | 40      | no  | no    | yes        | yes       | —          |
| markup   | 24      | no  | no    | no         | no        | 16         |
| doc      | 24      | no  | no    | no         | no        | 16         |

Add `.ptolemyignore` to any directory to exclude filenames by fnmatch pattern
(one pattern per line, `#` comments, applies to that directory only).

**Periodic checkpoint:** saved every 500 files during ingest.

---

## Daemon Mode

Start the daemon:

```bash
ptolemy -d                            # foreground (ctrl-C saves + exits)
ptolemy -d -q                         # quiet daemon
```

The daemon loads the checkpoint at startup, keeps the monad in memory, and
accepts queries over a Unix domain socket at `~/.ptolemy/ptolemy.sock`.
Checkpoint is saved on SIGTERM/SIGINT.

Query a running daemon:

```bash
ptolemy -D "information entropy systems"
```

Systemd socket activation is supported — if `$LISTEN_FDS` is set, fd 3 is
used directly (no libsystemd dependency):

```bash
systemctl --user enable --now ptolemy.socket
```

### Daemon line protocol

Commands over `~/.ptolemy/ptolemy.sock` (one line per command, `.\n` terminates
each response):

```
HEAR <prompt>      → Noether response
STATUS             → field status (vocab, A-edges, deepest β)
HEALTH             → full field health report
QUIT               → save checkpoint, close socket, exit
```

---

## Logging

Log files: `~/.ptolemy/logs/ptolemy_YYYYMMDD_HH00.log`

Slots rotate every 4 hours (0000 0400 0800 1200 1600 2000). On Sunday at
10:00, files not accessed or modified in the last 30 days are removed.

Suppress INFO output to stderr with `-q`.

---

## Examples

```bash
# Learn a file
ptolemy -l ~/Documents/notes.txt

# Learn stdin
echo "information entropy measures disorder" | ptolemy -l -

# Hear a query
ptolemy -h "what is entropy"

# Ingest a directory
ptolemy -I ~/Documents

# Ingest with verbose field output
ptolemy -Iv ~/Projects/Ptolemy/PtolC

# Field health
ptolemy -F

# Word lookup
ptolemy -w entropy

# Start daemon, then query it
ptolemy -d -q &
ptolemy -D "meaning of information"

# Full verbose: shows all β deepening, J^μ, A-edges, colours
ptolemy -lv ~/Documents/paper.txt -hv "main thesis"

# Self-referential: Ptolemy reads his own output
ptolemy -vvv -h "what are you"
```

---

## Environment Variables

| Variable | Default | Purpose |
|----------|---------|---------|
| `PTOLEMY_HOME` | `~/.ptolemy/` | Override home directory |
| `PTOLEMY_CHECKPOINT` | — | Override checkpoint path |

---

## Source Layout

```
PtolC/
  main.c          — CLI entry point, flag dispatch
  monad.c         — RedBlue Geometries Engine: learn/hear/speak
  monad.h         — Monad struct and API
  ptolemy.h       — Shared constants (N, φ, D*, Ω, strata)
  checkpoint.c    — Binary checkpoint load/save (v1/v2)
  tokenizer.c     — Text → lowercase alpha tokens
  filter.c        — Learn-time token filter (per-filetype rules)
  filter.h        — NSFiletype enum, FTRules struct
  ingest.c        — Filesystem walker, extractor dispatch
  ingest.h        — ingest_path() API
  daemon.c        — Unix socket daemon, systemd activation
  daemon.h        — Protocol constants and API
  log.c           — 4-hour rotating log, 30-day GC
  log.h           — plog() API
  man/            — ptolemy.1, ptolemyignore.5
  systemd/        — ptolemy.socket, ptolemy.service
  tools/          — checkpoint_expand, dump_wordnet.py, ingest_system.py
```

---

## Mathematical Background

The field engine operates on the Riemann zeros γ_k (imaginary parts of
non-trivial zeros of ζ(s) on σ=½):

- **Word addressing:** surface → bijective base-95 Horner integer n;
  seed = fmod(n × φ, 1.0); idx = floor(seed × N);
  E = D* + seed × (Ω − D*) where D*=0.24600, Ω=0.56714
- **β field deepening:** β[idx] += E × α (α=0.01); capped at β_sat=7.552
- **A matrix:** co-activated word pairs accumulate A[i,j] += E_i × E_j / |γ_i − γ_j|
- **Noether current:** J[idx] = β[idx] × E² × exp(−λ × age[idx]) (λ=0.05)
- **Response:** top-ranked J entries after A-propagation; forced by conservation

σ=½ is not chosen. It is forced by Noether balance: J_R + J_B + J₃ = 0.

Full derivation: [Ainulindalë](https://github.com/michaelrendier/Ainulindale)

---

*CLAUDE-SMMNIP-00729-56714-24600*
