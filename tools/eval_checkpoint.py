#!/usr/bin/env python3
"""
eval_checkpoint.py — RedBlue Geometries Engine checkpoint evaluator.

Parses a ptolemy .bin checkpoint and produces a structured assessment:
  - field health (entropy band, β distribution, Gini coefficient)
  - vocabulary quality (clean vs polluted token classification)
  - top tokens by β and top A-edges by weight
  - pollution breakdown by category
  - idempotency score vs a reference checkpoint (--compare)

Usage:
    python3 eval_checkpoint.py [checkpoint.bin] [--json] [--out report.json]
    python3 eval_checkpoint.py new.bin --compare old.bin   # idempotency delta

Outputs human-readable report by default; --json emits machine-readable JSON.

Verdict levels (all three criteria must pass for CONVERGED):
  CONVERGED  pollution < 1%  AND  entropy in natural band  AND  Gini in natural range
  PASS       pollution < 20% AND  no critical band violations
  WARN       pollution 20-40% OR  entropy/Gini out of band
  FAIL       pollution >= 40%

Target bands:
  Entropy % of H_max:  85-92%  (natural Zipf/prime distribution)
  Gini coefficient:    0.60-0.80  (right-skewed, not flat and not spiked)
  Pollution:           < 1%  (converged),  < 20%  (pass)
"""

import struct, re, sys, json, os, math, argparse
from datetime import datetime, timezone
from collections import Counter

# ── Checkpoint format (v2) ────────────────────────────────────────────────────
# magic[4] version[4] N[4] vocab_size[4] A_size[4] wc[4] threshold[8]
# beta: N×double  age: N×int32
# vocab: (idx[4] wlen[2] E[8] hs[1] gs[1] word[wlen]) × vocab_size
# A:    (ai[4] aj[4] aw[8]) × A_size

VOWELS = set('aeiouAEIOU')

def load_checkpoint(path):
    with open(path, 'rb') as f:
        magic      = f.read(4)
        ver,       = struct.unpack('<I', f.read(4))
        N,         = struct.unpack('<I', f.read(4))
        vocab_size,= struct.unpack('<I', f.read(4))
        A_size,    = struct.unpack('<I', f.read(4))
        wc,        = struct.unpack('<I', f.read(4))
        threshold, = struct.unpack('<d', f.read(8))

        if magic != b'PTOL':
            raise ValueError(f"Bad magic: {magic!r}")

        betas = struct.unpack(f'<{N}d', f.read(N * 8))
        ages  = struct.unpack(f'<{N}i', f.read(N * 4))

        vocab = []
        for _ in range(vocab_size):
            idx,  = struct.unpack('<I', f.read(4))
            wlen, = struct.unpack('<H', f.read(2))
            E,    = struct.unpack('<d', f.read(8))
            hs    = struct.unpack('<B', f.read(1))[0]
            gs    = struct.unpack('<B', f.read(1))[0]
            word  = f.read(wlen).decode('utf-8', errors='replace')
            vocab.append({
                'word': word, 'beta': betas[idx], 'E': E,
                'idx': idx, 'age': ages[idx],
                'home_stratum': hs, 'gen_stratum': gs,
            })

        edges = []
        for _ in range(A_size):
            ai, = struct.unpack('<I', f.read(4))
            aj, = struct.unpack('<I', f.read(4))
            aw, = struct.unpack('<d', f.read(8))
            edges.append((ai, aj, aw))

    return {
        'version': ver, 'N': N, 'vocab_size': vocab_size,
        'A_size': A_size, 'word_count': wc,
        'threshold': threshold, 'betas': betas,
        'vocab': vocab, 'edges': edges,
    }


# ── Token classification ───────────────────────────────────────────────────────

def classify(word):
    """Return list of pollution labels, or [] if clean."""
    labels = []
    core = word.strip("'")

    if len(word) == 0:
        return ['empty']
    if len(word) == 1:
        labels.append('single-char')
    if re.search(r'\d', word):
        labels.append('digit')
    if word.startswith("'") or word.endswith("'"):
        if len(core) <= 3:
            labels.append('apostrophe-fragment')
        else:
            labels.append('trailing-apostrophe')
    if len(core) >= 4 and not any(c in VOWELS for c in core):
        labels.append('no-vowel')
    elif len(core) == 3 and not any(c in VOWELS for c in core):
        labels.append('consonant-cluster-3')
    if re.fullmatch(r'[bcdfghjklmnpqrstvwxyz]{3,}', core.lower()):
        labels.append('consonant-only')
    if re.fullmatch(r'[a-f0-9]{4,}', word.lower()):
        labels.append('hex-string')
    if len(word) >= 6 and re.fullmatch(r'[a-z]{6,}', word) and \
            sum(1 for c in word if c in 'aeiou') / len(word) < 0.1:
        labels.append('low-vowel-ratio')

    return labels


# ── Field metrics ──────────────────────────────────────────────────────────────

def field_entropy(betas, N):
    """Shannon entropy over occupied zeros."""
    total = sum(b for b in betas if b > 0)
    if total == 0:
        return 0.0
    H = 0.0
    for b in betas:
        if b > 0:
            p = b / total
            H -= p * math.log2(p)
    return H


def gini_coefficient(betas):
    """Gini coefficient over occupied zeros (β above ground floor).

    0.0 = perfectly flat (noise / untrained)
    0.60-0.80 = natural Zipf/prime distribution (target)
    1.0 = single spike (degenerate)
    """
    ground_floor = abs(-1.888) / max(len(betas), 1) * 2
    occupied = sorted(b for b in betas if b > ground_floor)
    n = len(occupied)
    if n < 2:
        return 0.0
    total = sum(occupied)
    if total == 0:
        return 0.0
    numerator = sum((2 * i - n - 1) * b for i, b in enumerate(occupied, 1))
    return numerator / (n * total)


def entropy_band(entropy_pct):
    """Classify entropy % of H_max into a named band."""
    if entropy_pct < 75:
        return 'UNDERTRAINED'
    if entropy_pct < 85:
        return 'LOW'
    if entropy_pct <= 92:
        return 'NATURAL'
    if entropy_pct <= 96:
        return 'HIGH'
    return 'NOISY'


def gini_band(g):
    """Classify Gini coefficient into a named band."""
    if g < 0.30:
        return 'FLAT'
    if g < 0.60:
        return 'SHALLOW'
    if g <= 0.80:
        return 'NATURAL'
    return 'SPIKED'


def beta_distribution(betas, threshold, beta_sat):
    ground_floor = abs(-1.888) / max(len(betas), 1) * 2
    dist = Counter()
    for b in betas:
        if b <= ground_floor:          dist['ground'] += 1
        elif b < threshold * 0.5:     dist['low']    += 1
        elif b < threshold:           dist['mid']    += 1
        elif b < beta_sat:            dist['high']   += 1
        else:                         dist['sat']    += 1
    return dist


# ── Main evaluation ────────────────────────────────────────────────────────────

def evaluate(path):
    size_bytes = os.path.getsize(path)
    ck = load_checkpoint(path)

    vocab   = ck['vocab']
    N       = ck['N']
    betas   = ck['betas']
    edges   = ck['edges']
    beta_sat = 7.552

    # Classify all tokens
    for entry in vocab:
        entry['labels'] = classify(entry['word'])

    clean      = [e for e in vocab if not e['labels']]
    polluted   = [e for e in vocab if e['labels']]
    poll_pct   = 100 * len(polluted) / max(len(vocab), 1)

    # Pollution breakdown by category
    cat_counts = Counter()
    for e in polluted:
        for l in e['labels']:
            cat_counts[l] += 1

    # β distribution
    bdist = beta_distribution(betas, ck['threshold'], beta_sat)

    # Entropy
    H     = field_entropy(betas, N)
    H_max = math.log2(max(ck['vocab_size'], 1))
    H_pct = round(100 * H / H_max, 1) if H_max else 0

    # Gini coefficient
    G      = gini_coefficient(betas)
    G_band = gini_band(G)
    H_band = entropy_band(H_pct)

    # Top by β
    top_beta = sorted(vocab, key=lambda e: -e['beta'])[:20]

    # Top A-edges (need word lookup by idx)
    idx_to_word = {e['idx']: e['word'] for e in vocab}
    top_edges = sorted(edges, key=lambda e: -e[2])[:20]

    # Verdict
    in_natural_band = (H_band == 'NATURAL') and (G_band == 'NATURAL')
    if poll_pct >= 40.0:
        verdict = 'FAIL'
    elif poll_pct >= 20.0:
        verdict = 'WARN'
    elif not in_natural_band:
        verdict = 'PASS'
    elif poll_pct < 1.0:
        verdict = 'CONVERGED'
    else:
        verdict = 'PASS'

    return {
        'timestamp':    datetime.now(timezone.utc).isoformat(),
        'path':         os.path.abspath(path),
        'size_bytes':   size_bytes,
        'size_mb':      round(size_bytes / 1024**2, 2),
        'version':      ck['version'],
        'N':            N,
        'vocab_size':   ck['vocab_size'],
        'A_size':       ck['A_size'],
        'word_count':   ck['word_count'],
        'threshold':    ck['threshold'],
        'entropy_H':    round(H, 4),
        'entropy_H_max':round(H_max, 4),
        'entropy_pct':  H_pct,
        'entropy_band': H_band,
        'gini':         round(G, 4),
        'gini_band':    G_band,
        'beta_dist':    dict(bdist),
        'clean_count':  len(clean),
        'polluted_count': len(polluted),
        'pollution_pct':  round(poll_pct, 2),
        'pollution_by_category': dict(cat_counts),
        'top_beta': [
            {'word': e['word'], 'beta': round(e['beta'], 4), 'E': round(e['E'], 4),
             'labels': e['labels']}
            for e in top_beta
        ],
        'top_edges': [
            {'word_i': idx_to_word.get(ai, f'?{ai}'),
             'word_j': idx_to_word.get(aj, f'?{aj}'),
             'weight': round(aw, 4)}
            for ai, aj, aw in top_edges
        ],
        'verdict': verdict,
    }


# ── Report rendering ───────────────────────────────────────────────────────────

def render(r):
    lines = []
    lines.append(f"{'='*60}")
    lines.append(f"  Checkpoint Assessment — {r['timestamp'][:10]}")
    lines.append(f"{'='*60}")
    lines.append(f"  File       {r['path']}")
    lines.append(f"  Size       {r['size_mb']} MB")
    lines.append(f"  Version    v{r['version']}  N={r['N']}")
    lines.append(f"  Vocab      {r['vocab_size']:,}")
    lines.append(f"  A-edges    {r['A_size']:,}")
    lines.append(f"  Words seen {r['word_count']:,}")
    lines.append(f"")
    lines.append(f"  Entropy    {r['entropy_H']} / {r['entropy_H_max']} bits  "
                 f"({r['entropy_pct']}%)  [{r['entropy_band']}]")
    lines.append(f"  Gini       {r['gini']:.4f}  [{r['gini_band']}]")
    d = r['beta_dist']
    lines.append(f"  β dist     ground={d.get('ground',0)}  low={d.get('low',0)}  "
                 f"mid={d.get('mid',0)}  high={d.get('high',0)}  sat={d.get('sat',0)}")
    lines.append(f"")
    lines.append(f"  Clean      {r['clean_count']:,}  ({100-r['pollution_pct']:.1f}%)")
    lines.append(f"  Polluted   {r['polluted_count']:,}  ({r['pollution_pct']:.1f}%)")
    lines.append(f"  Verdict    {r['verdict']}")
    lines.append(f"")
    lines.append(f"  Target bands:")
    lines.append(f"    Entropy  85-92% of H_max  → {r['entropy_band']}")
    lines.append(f"    Gini     0.60-0.80         → {r['gini_band']}")
    lines.append(f"    Pollution < 1% (CONVERGED), < 20% (PASS)")
    lines.append(f"")
    lines.append(f"  Pollution by category:")
    for cat, n in sorted(r['pollution_by_category'].items(), key=lambda x: -x[1]):
        lines.append(f"    {cat:<28} {n:>5}")
    lines.append(f"")
    lines.append(f"  Top 20 tokens by β:")
    for e in r['top_beta']:
        flag = ' [POLLUTED: ' + ', '.join(e['labels']) + ']' if e['labels'] else ''
        lines.append(f"    β={e['beta']:.4f}  {e['word']:<24}{flag}")
    lines.append(f"")
    lines.append(f"  Top 10 A-edges by weight:")
    for e in r['top_edges'][:10]:
        lines.append(f"    {e['word_i']:<20} — {e['word_j']:<20}  w={e['weight']:.4f}")
    lines.append(f"{'='*60}")
    return '\n'.join(lines)


# ── Idempotency comparison ─────────────────────────────────────────────────────

def compare_checkpoints(path_new, path_ref):
    """Compare β vectors of two checkpoints. Returns idempotency report dict.

    The convergence criterion: repeated ingestion of the same corpus should
    move β < ε per zero. When Δβ_mean → 0, the field is an eigenfunction of H_RB.
    """
    ck_new = load_checkpoint(path_new)
    ck_ref = load_checkpoint(path_ref)

    N = min(ck_new['N'], ck_ref['N'])
    b_new = ck_new['betas'][:N]
    b_ref = ck_ref['betas'][:N]

    deltas = [abs(b_new[i] - b_ref[i]) for i in range(N)]
    mean_delta  = sum(deltas) / N
    max_delta   = max(deltas)
    moved_1e3   = sum(1 for d in deltas if d > 1e-3)
    moved_1e4   = sum(1 for d in deltas if d > 1e-4)
    converged   = mean_delta < 1e-3

    return {
        'path_new':     os.path.abspath(path_new),
        'path_ref':     os.path.abspath(path_ref),
        'N_compared':   N,
        'delta_mean':   round(mean_delta, 6),
        'delta_max':    round(max_delta, 6),
        'zeros_moved_1e3': moved_1e3,
        'zeros_moved_1e4': moved_1e4,
        'converged':    converged,
        'verdict':      'CONVERGED' if converged else 'DRIFTING',
    }


def render_compare(c):
    lines = [
        f"{'='*60}",
        f"  Idempotency Delta",
        f"{'='*60}",
        f"  New   {c['path_new']}",
        f"  Ref   {c['path_ref']}",
        f"  N     {c['N_compared']:,}  zeros compared",
        f"",
        f"  Δβ mean   {c['delta_mean']:.6f}",
        f"  Δβ max    {c['delta_max']:.6f}",
        f"  Zeros moved > 1e-3:  {c['zeros_moved_1e3']:,}",
        f"  Zeros moved > 1e-4:  {c['zeros_moved_1e4']:,}",
        f"",
        f"  Criterion: Δβ_mean < 0.001 → eigenstate reached",
        f"  Verdict   {c['verdict']}",
        f"{'='*60}",
    ]
    return '\n'.join(lines)


# ── Entry point ────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checkpoint', nargs='?',
                        default=os.path.expanduser('~/.ptolemy/monad_wordnet.bin'))
    parser.add_argument('--json', action='store_true', help='emit JSON only')
    parser.add_argument('--out', metavar='FILE', help='write JSON report to file')
    parser.add_argument('--compare', metavar='REF',
                        help='compare β vectors against reference checkpoint (idempotency test)')
    args = parser.parse_args()

    if not os.path.isfile(args.checkpoint):
        print(f"error: {args.checkpoint} not found", file=sys.stderr)
        sys.exit(1)

    if args.compare:
        if not os.path.isfile(args.compare):
            print(f"error: {args.compare} not found", file=sys.stderr)
            sys.exit(1)
        cmp = compare_checkpoints(args.checkpoint, args.compare)
        if args.json:
            print(json.dumps(cmp, indent=2))
        else:
            print(render_compare(cmp))
        sys.exit(0 if cmp['converged'] else 1)

    result = evaluate(args.checkpoint)

    if args.out:
        with open(args.out, 'w') as f:
            json.dump(result, f, indent=2)
        print(f"report written → {args.out}")

    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print(render(result))

    sys.exit(0 if result['verdict'] in ('PASS', 'CONVERGED') else 1)


if __name__ == '__main__':
    main()
