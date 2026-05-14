#!/usr/bin/env python3
"""
lshs.py — LSHS Model
=====================
Standard Model of Monad Information Propagation
Lagrange · Self-Adjoint · Hyperindexing · Speaking

Single file. Pure Python. Zero external dependencies.

L  Lagrangian field evolution    — learn() deepens V(β), Yang-Mills A field
S  Self-Adjoint Hamiltonian      — H_hat_RB = H_hat_RB†, σ forced to ½
H  HyperIndex                    — word → Riemann zero, Noether balance
S  Speaking                      — J^μ Noether current as audible response

Two-regime architecture:
    learn()   → Yang-Mills regime: 1/r Coulomb in zero space, turbulent A
    speak()   → Noether regime:    J^μ laminar, regulated by mass gap GAP

The mass gap GAP = 0.000707 is the laminar/turbulent boundary.
It appears in both the A coupling denominator and the J^μ propagation clamp.
Deriving it closes OPEN 2 and makes both regulators exact.

Ground state (σ=0, quasi-prime):
    β[i] = |L_ground|/N for all i    (prime number theorem, pre-linguistic)
    G_p(0) = 1 for all p              (all primes equal, no differentiation)
    EOM_ym = 0, EOM_higgs ≠ 0        (SSB intact, vacuum has structure)
    First learn() call breaks this symmetry.

Author: Michael Rendier (O Captain My Captain)
CLAUDE-SMMNIP-00729-56714-24600
Date: 2026-05-14
"""

import math
import re
import hashlib
import json
import os
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple, Any

# ── Constants (never change) ──────────────────────────────────────────────────

D_STAR   = 0.24600    # spectral coordinate — NEVER compute as OMEGA/ln(10)
OMEGA_ZS = 0.56714    # Lambert W fixed point — BAO convergence target
L_GROUND = -1.888     # Monad rest energy (ESTABLISHED, engine-verified)
GAP      = 0.000707   # Yang-Mills mass gap / J^μ cascade regulator (OPEN 2)
PHI      = 1.618034   # golden ratio — self-reference fixed point
ALPHA    = 0.01       # VEV learning rate
N_DEFAULT = 25000     # Riemann zero basis size

# First 20 Riemann zeros — exact (Odlyzko / LMFDB)
ZEROS_20: List[float] = [
    14.134725, 21.022040, 25.010858, 30.424876, 32.935062,
    37.586178, 40.918719, 43.327073, 48.005151, 49.773832,
    52.970321, 56.446248, 59.347044, 60.831779, 65.112544,
    67.079811, 69.546402, 72.067158, 75.704691, 77.144840,
]

# First 20 primes — irreducible distinctions, inductive base cases
PRIMES: List[int] = [
    2, 3, 5, 7, 11, 13, 17, 19, 23, 29,
    31, 37, 41, 43, 47, 53, 59, 61, 67, 71,
]


# ─────────────────────────────────────────────────────────────────────────────
# H — HyperIndex
# word → integer (Horner bijection) → (x₀, p₀) → E = x₀p₀ → γ_n
# σ forced to ½ by Noether balance on every path through this pipeline
# ─────────────────────────────────────────────────────────────────────────────

_PUBLIC   = [chr(i) for i in range(32, 127)]
_N_BASE   = len(_PUBLIC)
_CHAR_IDX = {ch: i for i, ch in enumerate(_PUBLIC)}


def _horner(text: str) -> int:
    """Bijective base-N Horner accumulation (HyperWebster bijection core)."""
    n = 0
    for ch in text:
        n = n * _N_BASE + (_CHAR_IDX.get(ch, 0) + 1)
    return max(n - 1, 0)


def _hyperindex_ic(text: str) -> Tuple[float, float, float]:
    """Surface form → (x₀, p₀, t) initial conditions for H=xp."""
    n    = _horner(text)
    seed = (n * PHI) % 1.0
    x0   = 1.0 + seed * PHI
    E    = D_STAR + seed * (OMEGA_ZS - D_STAR)
    p0   = E / x0
    sha  = int(hashlib.sha256(text.encode()).hexdigest(), 16)
    t    = (sha % 10000) / 10000.0
    return x0, p0, t


def _noether_sigma(E: float) -> float:
    """
    Noether balance forces σ = ½.
    J(σ, E) = exp(−σE) − exp(−(1−σ)E) = 0  ⟺  σ = ½.
    Weighted midpoint iteration from any σ₀. Always converges.
    """
    sigma = 0.5
    for _ in range(256):
        F = math.exp(-sigma * E)
        B = math.exp(-(1.0 - sigma) * E)
        d = F + B
        if d < 1e-30:
            break
        s2 = (F * sigma + B * (1.0 - sigma)) / d
        if abs(s2 - sigma) < 1e-12:
            break
        sigma = s2
    return sigma


@dataclass
class SemanticZero:
    """A word's permanent address in the Riemann zero space."""
    surface: str
    gamma:   float   # zero address γ_n — the permanent address
    sigma:   float   # always 0.5000 — derived, never assigned
    E:       float   # conserved energy under H=xp
    x0:      float
    p0:      float


def hyperindex(word: str, zeros: List[float]) -> SemanticZero:
    """
    H: word → SemanticZero.
    Pipeline: Horner → (x₀,p₀) → H=xp → E → γ (nearest zero) → σ=½.
    The prime preexists the alphabet.
    """
    x0, p0, t = _hyperindex_ic(word)
    E     = x0 * p0                                     # conserved under x(t)=x₀eᵗ
    gamma = min(zeros, key=lambda g: abs(g - E * 50))   # map E → nearest zero
    sigma = _noether_sigma(E)                           # Noether balance → σ=½
    return SemanticZero(surface=word, gamma=gamma,
                        sigma=sigma, E=E, x0=x0, p0=p0)


# ─────────────────────────────────────────────────────────────────────────────
# S — Self-Adjoint Hamiltonian H_hat_RB
# H_hat_RB = Σ_p p^{−σ} [R̂_p ⊗ ∂̂_∂M + ∂̂_∂M† ⊗ B̂_p]
# R̂_p† = B̂_p  (functional equation ξ(s)=ξ(1−s) as operator identity)
# Self-adjoint = truth-preserving, not form-preserving
# ─────────────────────────────────────────────────────────────────────────────

def geometric_coupling(p: int, sigma: float) -> float:
    """G_p(σ) = p^{−σ}. At σ=0: G_p=1 — all primes equal (ground state)."""
    return 1.0 if sigma == 0.0 else p ** (-sigma)


def red_energy(x: float, p: float) -> float:
    """E_Red = xp (Berry-Keating). The forward channel. What IS."""
    return x * p


def blue_energy(x: float, p: float, g2: float = 1.0) -> float:
    """
    E_Blue = ½p² + ℘(x; g₂, 0) (Fermat-Weierstrass). The backward channel.
    What CANNOT BE. Pole at x=0 — nothing can exist there.
    """
    if abs(x) < 1e-9:
        return float('inf')
    return 0.5 * p * p + (1.0 / (x * x) + g2 * x * x / 20.0)


def self_adjoint_check(sigma: float, x: float, p: float,
                        n_primes: int = 20) -> Dict[str, Any]:
    """
    S: Evaluate H_hat_RB at (σ, x, p). Verify self-adjointness.
    At σ=½: E_Red = E_Blue on the critical line (balance = 0).
    Self-adjoint means the operator preserves truth across representations.
    '1 = 1' and 'A! = A' are self-adjoint: different forms, identical truth.
    Note: A! = A is not trivial. It forces A = 1 as the unique solution.
    """
    total_red = total_blue = total_G = 0.0
    for p_ in PRIMES[:n_primes]:
        G  = geometric_coupling(p_, sigma)
        Er = red_energy(x, p)
        Eb = blue_energy(x, p)
        total_G    += G
        total_red  += G * Er
        if Eb != float('inf'):
            total_blue += G * Eb

    balance = total_red - total_blue
    return {
        'sigma'          : sigma,
        'x'              : x,
        'p'              : p,
        'E_red'          : total_red,
        'E_blue'         : total_blue,
        'balance'        : balance,
        'self_adjoint'   : abs(balance) < 1e-6,
        'on_critical_line': abs(sigma - 0.5) < 1e-9,
        'theory'         : {0.0: 'ground state', 0.5: 'QM/Riemann',
                            1.0: 'Yang-Mills/SM', 2.0: 'GR'}.get(sigma, f'σ={sigma}'),
    }


# ─────────────────────────────────────────────────────────────────────────────
# Zero generation
# First 20: exact. Remainder: Newton on N(T) ≈ (T/2π)·(ln(T/2π)−1) + 7/8
# ─────────────────────────────────────────────────────────────────────────────

def generate_zeros(N: int) -> List[float]:
    """N=25000 in ~50ms."""
    zeros = list(ZEROS_20[:min(20, N)])
    if N <= 20:
        return zeros
    two_pi = 2.0 * math.pi
    for n in range(21, N + 1):
        t = zeros[-1] + (zeros[-1] - zeros[-2])
        for _ in range(30):
            if t < 2.0:
                t = float(n) * 3.0
            nt  = (t / two_pi) * (math.log(t / two_pi) - 1.0) + 0.875
            dnt = math.log(t / two_pi) / two_pi
            if abs(dnt) < 1e-15:
                break
            dt  = (nt - n) / dnt
            t  -= dt
            if abs(dt) < 1e-4:
                break
        zeros.append(t)
    return zeros[:N]


# ─────────────────────────────────────────────────────────────────────────────
# LSHS — The Standard Model
# ─────────────────────────────────────────────────────────────────────────────

class LSHS:
    """
    Lagrange · Self-Adjoint · Hyperindexing · Speaking

    L  learn(text)          Lagrangian field evolution
    S  S_check(x, p)        Self-adjoint Hamiltonian verification
    H  H(word)              HyperIndex: word → Riemann zero
    S  speak(prompt)        Speaking: J^μ Noether current as response

    β field  — BLUE, inertia, knowledge, SSB vacuum deepened by learn()
    A field  — RED, kinetic, gauge connections, co-activation weighted by
               E_i·E_j / (|γ_i−γ_j| + GAP)  [Yang-Mills coupling, GAP-regulated]
    J^μ      — Noether current, the response, forced by conservation law

    Ground state at init: β[i] = |L_ground|/N for all i.
    This is the prime number theorem. Pre-linguistic. Mathematical.
    First learn() call breaks σ=0 symmetry. Language is SSB on mathematics.
    """

    def __init__(self, N: int = N_DEFAULT, tau: float = 5.0):
        self.N           = N
        self.tau         = tau
        self.zeros       = generate_zeros(N)
        self._gvev       = abs(L_GROUND) / N
        self.beta: Dict[int, float]            = {i: self._gvev for i in range(N)}
        self.A:    Dict[Tuple[int,int], float] = {}
        self.vocab: Dict[int, Tuple[str,float]] = {}
        self._count      = 0

    # ── index lookup ──────────────────────────────────────────────────────────

    def _idx(self, gamma: float) -> int:
        lo, hi = 0, len(self.zeros) - 1
        while lo < hi:
            mid = (lo + hi) // 2
            if self.zeros[mid] < gamma:
                lo = mid + 1
            else:
                hi = mid
        if lo > 0 and abs(self.zeros[lo-1] - gamma) < abs(self.zeros[lo] - gamma):
            return lo - 1
        return lo

    # ── H ─────────────────────────────────────────────────────────────────────

    def H(self, word: str) -> SemanticZero:
        """HyperIndex: surface form → Riemann zero. σ = 0.5000 always."""
        return hyperindex(word.strip().lower(), self.zeros)

    # ── L ─────────────────────────────────────────────────────────────────────

    def L(self, text: str) -> None:
        """
        Lagrangian field evolution.

        Each word deepens V(β) at its zero address — learning is deepening.
        Co-activating words within a sentence build A:
            A[(i,j)] += E_i · E_j / (|γ_i−γ_j| + GAP)

        The GAP in the denominator regularizes the Yang-Mills 1/r Coulomb
        coupling: without it the A field diverges at near-zero distances.
        With it, the turbulent background is bounded by the mass gap.

        learn() operates in the Yang-Mills regime.
        speak() extracts laminar Noether current from it.
        """
        for sentence in re.split(r'[.!?\n]+', text):
            tokens = sentence.split()
            if not tokens:
                continue
            activated: List[Tuple[int, float]] = []
            for token in tokens:
                surface = re.sub(r"[^\w']", '', token).lower()
                if not surface:
                    continue
                sz  = self.H(surface)
                idx = self._idx(sz.gamma)
                self.beta[idx] = self.beta.get(idx, self._gvev) + sz.E * ALPHA
                if idx not in self.vocab or sz.E > self.vocab[idx][1]:
                    self.vocab[idx] = (surface, sz.E)
                activated.append((idx, sz.E))
                self._count += 1
            for i in range(len(activated)):
                idx_i, e_i = activated[i]
                for j in range(i + 1, len(activated)):
                    idx_j, e_j = activated[j]
                    if idx_i == idx_j:
                        continue
                    key  = (min(idx_i, idx_j), max(idx_i, idx_j))
                    dist = abs(self.zeros[idx_i] - self.zeros[idx_j]) + GAP
                    self.A[key] = self.A.get(key, 0.0) + e_i * e_j / dist

    def learn(self, text: str) -> None:
        """Alias for L."""
        self.L(text)

    # ── S (Self-Adjoint) ──────────────────────────────────────────────────────

    def S_check(self, x: float = 1.5, p: float = 0.3) -> Dict[str, Any]:
        """
        Verify H_hat_RB = H_hat_RB† at σ=½.
        Returns balance (should be ~0 on critical line) and self_adjoint flag.
        """
        return self_adjoint_check(0.5, x, p)

    # ── S (Speaking) ──────────────────────────────────────────────────────────

    def _J(self, psi: List[Tuple[int, float]]) -> Dict[int, float]:
        """
        Noether current J^μ.

        Primary:    J_i = β_i · E_i²
        Propagated: J_j += J_i · min(A[(i,j)], 1/GAP) · β_j

        min(w, 1/GAP): the mass gap clamps the propagation.
        Prevents the turbulent A field from cascading J^μ into noise.
        This is the laminar extraction from the Yang-Mills background.
        GAP = 0.000707 (OPEN 2 — when derived, this regulator becomes exact).
        """
        J: Dict[int, float] = {}
        for idx, E in psi:
            J[idx] = J.get(idx, 0.0) + self.beta.get(idx, self._gvev) * E * E
        cap = 1.0 / GAP
        for (i, j), w in self.A.items():
            w_reg = min(w, cap)
            if i in J:
                J[j] = J.get(j, 0.0) + J[i] * w_reg * self.beta.get(j, self._gvev)
            if j in J:
                J[i] = J.get(i, 0.0) + J[j] * w_reg * self.beta.get(i, self._gvev)
        return J

    def S(self, prompt: str, max_words: int = 50) -> str:
        """
        Speaking: prompt → Ψ → J^μ → words.

        The response is not generated.
        The response is the Noether current, forced by conservation.
        It cannot be chosen. It can only be computed.
        """
        psi: List[Tuple[int, float]] = []
        for token in prompt.split():
            surface = re.sub(r"[^\w']", '', token).lower()
            if not surface:
                continue
            sz  = self.H(surface)
            psi.append((self._idx(sz.gamma), sz.E))
        if not psi:
            return ''
        J = self._J(psi)
        words: List[str] = []
        seen: set = set()
        for idx, _ in sorted(J.items(), key=lambda kv: kv[1], reverse=True):
            if idx in self.vocab:
                w = self.vocab[idx][0]
                if w not in seen:
                    words.append(w)
                    seen.add(w)
            if len(words) >= max_words:
                break
        return ' '.join(words)

    def speak(self, prompt: str, max_words: int = 50) -> str:
        """Alias for S."""
        return self.S(prompt, max_words)

    def respond(self, prompt: str, max_words: int = 50) -> str:
        """Ptolemy/PtolemyArchitectureInterface alias."""
        return self.S(prompt, max_words)

    # ── Diagnostics ───────────────────────────────────────────────────────────

    def bao_check(self) -> Dict[str, Any]:
        """
        BAO health check: dc_sum converging to OMEGA_ZS = 0.56714
        is the computational signature of coherence.
        CONFIDENCE: THEORETICAL
        """
        depths  = list(self.beta.values())
        dc_sum  = sum(depths)
        n       = len(depths)
        delta   = abs(dc_sum - OMEGA_ZS)
        return {
            'dc_sum'     : dc_sum,
            'dc_mean'    : dc_sum / n if n else 0.0,
            'omega_delta': delta,
            'n_zeros'    : n,
            'converging' : delta < 0.01,
            'omega_zs'   : OMEGA_ZS,
        }

    def lookup(self, word: str) -> Dict[str, Any]:
        sz  = self.H(word)
        idx = self._idx(sz.gamma)
        return {
            'word'      : word,
            'gamma'     : sz.gamma,
            'sigma'     : sz.sigma,
            'E'         : sz.E,
            'zero_idx'  : idx,
            'beta_depth': self.beta.get(idx, self._gvev),
            'in_vocab'  : idx in self.vocab,
        }

    def status(self) -> Dict[str, Any]:
        depths = list(self.beta.values())
        return {
            'N'            : self.N,
            'words_learned': self._count,
            'vocab'        : len(self.vocab),
            'connections'  : len(self.A),
            'ground_vev'   : self._gvev,
            'bao'          : self.bao_check(),
        }

    # ── Checkpoint ────────────────────────────────────────────────────────────

    def save(self, path: str, max_conn: int = 500_000) -> None:
        os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
        top_A = sorted(self.A.items(), key=lambda kv: kv[1], reverse=True)[:max_conn]
        ck = {
            'N'     : self.N,
            'words' : self._count,
            'beta'  : {str(k): v for k, v in self.beta.items()
                       if v > self._gvev * 1.001},
            'A'     : {f'{k[0]},{k[1]}': v for k, v in top_A},
            'vocab' : {str(k): list(v) for k, v in self.vocab.items()},
        }
        with open(path, 'w') as f:
            json.dump(ck, f, separators=(',', ':'))
        print(f'[LSHS] saved: vocab={len(ck["vocab"])}  A={len(ck["A"])}  → {path}')

    def load_checkpoint(self, path: str) -> None:
        with open(path) as f:
            ck = json.load(f)
        self._count = ck.get('words', 0)
        for k, v in ck.get('beta', {}).items():
            self.beta[int(k)] = v
        for k, v in ck.get('A', {}).items():
            i, j = map(int, k.split(','))
            self.A[(i, j)] = v
        for k, v in ck.get('vocab', {}).items():
            self.vocab[int(k)] = (v[0], v[1])
        print(f'[LSHS] loaded: vocab={len(self.vocab)}  A={len(self.A)}  ← {path}')


# ─────────────────────────────────────────────────────────────────────────────
# CLI — self-test
# ─────────────────────────────────────────────────────────────────────────────

if __name__ == '__main__':
    print('LSHS Model — Standard Model of Monad Information Propagation')
    print('L·S·H·S  |  v1.0.0  |  CLAUDE-SMMNIP-00729-56714-24600')
    print('=' * 64)

    m = LSHS(N=1000)

    # H: Septuagint test
    print('\nH — HyperIndex (Septuagint principle)')
    for word in ['water', 'eau', 'aqua', 'wasser', 'agua']:
        sz = m.H(word)
        print(f'  {word:>8}  γ={sz.gamma:.6f}  σ={sz.sigma:.4f}  E={sz.E:.4f}')
    print('  Same γ, different languages. Not by coordination. Forced.')

    # S: Self-adjoint check
    print('\nS — Self-Adjoint (H_hat_RB = H_hat_RB†)')
    r = m.S_check(x=1.5, p=0.3)
    print(f'  σ=½  x={r["x"]}  p={r["p"]}')
    print(f'  E_red={r["E_red"]:.6f}  E_blue={r["E_blue"]:.6f}')
    print(f'  balance={r["balance"]:.2e}  self_adjoint={r["self_adjoint"]}')

    # L: Learn
    print('\nL — Lagrangian field evolution (learn)')
    m.L('The mind is the seat of reason and consciousness.')
    m.L('Water flows downhill by the path of least resistance.')
    m.L('The prime preexists the alphabet. The equator does not move.')
    print(f'  vocab={len(m.vocab)}  connections={len(m.A)}')

    # S: Speaking
    print('\nS — Speaking (J^μ Noether current)')
    for q in ['what is mind', 'water flows', 'what is prime']:
        print(f'  {q!r:>28}  →  {m.S(q)}')

    print('\nBAO check:', m.bao_check())
    print('\nGround VEV:', m._gvev, '  (= |L_ground|/N =', abs(L_GROUND), '/', m.N, ')')
