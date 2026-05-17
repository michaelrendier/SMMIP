/*
 * PtolC/ptolemy.h — Shared constants for the C Monad.
 *
 * All values mirror Philadelphos/monad.py exactly.
 * Do not change these without changing the Python source.
 */

#ifndef PTOLEMY_H
#define PTOLEMY_H

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MONAD_N_DEFAULT      25000
#define MONAD_L_GROUND      (-1.888)
#define MONAD_D_STAR         0.24600
#define MONAD_OMEGA_ZS       0.56714
#define MONAD_PHI            1.6180339887498948482
#define MONAD_ALPHA_LEARN    0.01
#define MONAD_LAMBDA         0.05
#define MONAD_TAU            5.0

/* Spectral neighbourhood spread — how far J diffuses along the zero spectrum.
 * RADIUS zeros on each side of the activated zero receive decayed J.
 * Weight at distance d = exp(-DECAY * d):  d=1 → 0.37,  d=2 → 0.14,  d=3 → 0.05 */
#define MONAD_SPREAD_RADIUS  3
#define MONAD_SPREAD_DECAY   1.0

/* Yang-Mills mass gap regulator.
 * GAP = |Ω − d* × ln(10)| = |0.56714 − 0.24600 × 2.30259| = 0.000707 ≈ 1/(1000√2)
 * Used as the floor in Coulomb coupling to prevent divergence when two words
 * share the same or adjacent zero, and to clamp J propagation in speak(). */
#define MONAD_GAP  0.000707

/* Co-occurrence window for A-edge creation.
 * Edges are created only between words within WINDOW_MAX token positions of
 * each other in the text stream.  The text distance d = j - i enters the
 * coupling formula directly, creating a 2D Coulomb law:
 *   w = Eᵢ × Eⱼ / ((|γᵢ − γⱼ| + GAP) × d)
 * Words close in BOTH zero-space and text-space get the strongest A-edges.
 * Window size 7–12 is the stable attractor cusp: below 7 is too local
 * (no cross-sentence reach), above 12 dissolves into noise. */
#define MONAD_WINDOW_MIN  7
#define MONAD_WINDOW_MAX  12

/* β_sat = |L_GROUND| * 4 */
#define MONAD_BETA_SAT       7.552

/* Emission threshold = |L_GROUND| * 2 */
#define MONAD_EMIT_THRESH    3.776

/* Max word/token byte length stored in vocab */
#define MAX_WORD_LEN         256

/* Binary checkpoint magic and version */
#define CKPT_MAGIC           "PTOL"
#define CKPT_VERSION         3   /* v3: VocabEntry adds prose_seen flag */

/* Native Space — Dixon tower strata (Cayley-Dickson doubling: ℝ→ℂ→ℍ→𝕆→𝕊).
 * All Hamiltonian expressions live in Native Space (radial complex spherical
 * polar coordinates).  Cartesian output is a terminal projection only. */
#define NS_SIGMA_R           0   /* σ₀  ℝ   — real, enumerable          */
#define NS_SIGMA_C           1   /* σ₁  ℂ   — complex, relational        */
#define NS_SIGMA_H           2   /* σ₂  ℍ   — quaternion, non-commuting  */
#define NS_SIGMA_O           3   /* σ₃  𝕆   — octonion, non-associating  */
#define NS_SIGMA_S           4   /* σ₄  𝕊   — sedenion, non-alternative  */

/* Default stratum for natural language tokens learned from prose text.
 * Language is relational → complex plane → σ₁. */
#define NS_SIGMA_TEXT        NS_SIGMA_C

/* ANSI colour codes — used when stderr/stdout is a tty */
#define C_RESET    "\033[0m"
#define C_BOLD     "\033[1m"
#define C_DIM      "\033[2m"
#define C_YELLOW   "\033[33m"   /* learn()  */
#define C_CYAN     "\033[36m"   /* hear()   */
#define C_GREEN    "\033[32m"   /* speak()  */
#define C_MAGENTA  "\033[35m"   /* J^mu propagation */
#define C_WHITE    "\033[97m"   /* numbers  */

#endif /* PTOLEMY_H */
