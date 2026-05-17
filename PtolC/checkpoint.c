/*
 * PtolC/checkpoint.c — Binary checkpoint loader/saver.
 *
 * Format (all little-endian):
 *   Header:  magic[4] version[4] N[4] vocab_size[4] A_size[4] wc[4] threshold[8]
 *   Beta:    N * double
 *   Age:     N * int32
 *   Vocab:   vocab_size * (idx[4] wlen[2] E[8] home_stratum[1] gen_stratum[1] word[wlen])
 *            v1 omits the two stratum bytes; loader defaults both to NS_SIGMA_TEXT.
 *   A:       A_size * (i[4] j[4] weight[8])
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ptolemy.h"
#include "checkpoint.h"
#include "monad.h"

static int read32(FILE *f, uint32_t *v)
{
    return fread(v, 4, 1, f) == 1 ? 0 : -1;
}

static int read16(FILE *f, uint16_t *v)
{
    return fread(v, 2, 1, f) == 1 ? 0 : -1;
}

static int read64d(FILE *f, double *v)
{
    return fread(v, 8, 1, f) == 1 ? 0 : -1;
}

/* ── next_pow2 helper ─────────────────────────────────────────────────────── */
static int next_pow2(int n)
{
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

/* ── Load ─────────────────────────────────────────────────────────────────── */

int checkpoint_load(Monad *m, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[checkpoint] cannot open %s\n", path);
        return -1;
    }

    /* Magic */
    char magic[5] = {0};
    if (fread(magic, 1, 4, f) != 4 || strncmp(magic, CKPT_MAGIC, 4) != 0) {
        fprintf(stderr, "[checkpoint] bad magic in %s\n", path);
        fclose(f); return -1;
    }

    /* Header fields */
    uint32_t version, N, vocab_size, A_size, word_count;
    double   threshold;
    if (read32(f, &version)    || read32(f, &N)          ||
        read32(f, &vocab_size) || read32(f, &A_size)     ||
        read32(f, &word_count) || read64d(f, &threshold)) {
        fprintf(stderr, "[checkpoint] header read error\n");
        fclose(f); return -1;
    }

    if ((int)N != m->N) {
        fprintf(stderr, "[checkpoint] N mismatch: file=%u monad=%d\n", N, m->N);
        fclose(f); return -1;
    }

    m->word_count         = (int)word_count;
    m->emission_threshold = threshold;

    /* Beta */
    if (fread(m->beta, sizeof(double), N, f) != N) {
        fprintf(stderr, "[checkpoint] beta read error\n");
        fclose(f); return -1;
    }

    /* Age */
    if (fread(m->age, sizeof(int), N, f) != N) {
        fprintf(stderr, "[checkpoint] age read error\n");
        fclose(f); return -1;
    }

    /* Expand A map to fit checkpoint entries */
    if ((int)A_size > 0) {
        int new_cap = next_pow2((int)A_size * 2);
        if (new_cap > m->am_cap) {
            free(m->am);
            m->am_cap = new_cap;
            m->am     = calloc(m->am_cap, sizeof(ASlot));
            if (!m->am) {
                fprintf(stderr, "[checkpoint] OOM: cannot allocate A matrix (%d slots)\n",
                        m->am_cap);
                fclose(f); return -1;
            }
        }
    }

    /* Expand word map */
    if ((int)vocab_size > 0) {
        int new_cap = next_pow2((int)vocab_size * 2);
        if (new_cap > m->wm_cap) {
            for (int i = 0; i < m->wm_cap; i++)
                if (m->wm[i].key) free(m->wm[i].key);
            free(m->wm);
            m->wm_cap  = new_cap;
            m->wm      = calloc(m->wm_cap, sizeof(WMSlot));
            if (!m->wm) {
                fprintf(stderr, "[checkpoint] OOM: cannot allocate word map (%d slots)\n",
                        m->wm_cap);
                fclose(f); return -1;
            }
            m->wm_size = 0;
        }
    }

    /* Vocab entries — v1: no stratum; v2: +home_stratum[1]+gen_stratum[1]; v3: +prose_seen[1] */
    for (uint32_t i = 0; i < vocab_size; i++) {
        uint32_t idx;
        uint16_t wlen;
        double   E;
        if (read32(f, &idx) || read16(f, &wlen) || read64d(f, &E)) {
            fprintf(stderr, "[checkpoint] vocab entry %u read error\n", i);
            fclose(f); return -1;
        }
        uint8_t hs = NS_SIGMA_TEXT, gs = NS_SIGMA_TEXT, ps = 0;
        if (version >= 2) {
            if (fread(&hs, 1, 1, f) != 1 || fread(&gs, 1, 1, f) != 1) {
                fprintf(stderr, "[checkpoint] vocab stratum read error at entry %u\n", i);
                fclose(f); return -1;
            }
        }
        if (version >= 3) {
            if (fread(&ps, 1, 1, f) != 1) {
                fprintf(stderr, "[checkpoint] vocab prose_seen read error at entry %u\n", i);
                fclose(f); return -1;
            }
        }
        if (wlen >= MAX_WORD_LEN) wlen = MAX_WORD_LEN - 1;
        char word[MAX_WORD_LEN];
        if (fread(word, 1, wlen, f) != wlen) {
            fprintf(stderr, "[checkpoint] vocab word read error\n");
            fclose(f); return -1;
        }
        word[wlen] = '\0';

        if ((int)idx < m->N) {
            m->vocab[idx].E            = E;
            m->vocab[idx].present      = 1;
            m->vocab[idx].home_stratum = hs;
            m->vocab[idx].gen_stratum  = gs;
            m->vocab[idx].prose_seen   = ps;
            memcpy(m->vocab[idx].word, word, wlen);
            m->vocab[idx].word[wlen] = '\0';
            monad_wm_set(m, word, idx);
        }
    }

    /* A edges */
    for (uint32_t i = 0; i < A_size; i++) {
        uint32_t ai, aj;
        double   aw;
        if (read32(f, &ai) || read32(f, &aj) || read64d(f, &aw)) {
            fprintf(stderr, "[checkpoint] A entry %u read error\n", i);
            fclose(f); return -1;
        }
        if ((int)ai < m->N && (int)aj < m->N)
            monad_a_add(m, (int)ai, (int)aj, aw);
    }

    fclose(f);

    int vocab_count = 0;
    for (int i = 0; i < m->N; i++) if (m->vocab[i].present) vocab_count++;
    fprintf(stderr, "[checkpoint] loaded %s  vocab=%d  A=%d  wc=%d\n",
            path, vocab_count, m->am_size, m->word_count);
    return 0;
}

/* ── Save ─────────────────────────────────────────────────────────────────── */

int checkpoint_save(const Monad *m, const char *path, double min_weight)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[checkpoint] cannot write %s\n", path);
        return -1;
    }

    /* Count vocab and A entries to save */
    int vocab_count = 0;
    for (int i = 0; i < m->N; i++)
        if (m->vocab[i].present) vocab_count++;

    int a_count = 0;
    for (int i = 0; i < m->am_cap; i++)
        if (m->am[i].key != 0 && m->am[i].val >= min_weight) a_count++;

    /* Header */
    uint32_t version = CKPT_VERSION;
    uint32_t N       = (uint32_t)m->N;
    uint32_t vc      = (uint32_t)vocab_count;
    uint32_t ac      = (uint32_t)a_count;
    uint32_t wc      = (uint32_t)m->word_count;
    fwrite(CKPT_MAGIC, 1, 4, f);
    fwrite(&version,   4, 1, f);
    fwrite(&N,         4, 1, f);
    fwrite(&vc,        4, 1, f);
    fwrite(&ac,        4, 1, f);
    fwrite(&wc,        4, 1, f);
    fwrite(&m->emission_threshold, 8, 1, f);

    /* Beta */
    fwrite(m->beta, sizeof(double), m->N, f);

    /* Age */
    fwrite(m->age, sizeof(int), m->N, f);

    /* Vocab — v3: idx[4] wlen[2] E[8] home_stratum[1] gen_stratum[1] prose_seen[1] word[wlen] */
    for (int i = 0; i < m->N; i++) {
        if (!m->vocab[i].present) continue;
        uint32_t idx  = (uint32_t)i;
        uint16_t wlen = (uint16_t)strlen(m->vocab[i].word);
        double   E    = m->vocab[i].E;
        uint8_t  hs   = m->vocab[i].home_stratum;
        uint8_t  gs   = m->vocab[i].gen_stratum;
        uint8_t  ps   = m->vocab[i].prose_seen;
        fwrite(&idx,  4, 1, f);
        fwrite(&wlen, 2, 1, f);
        fwrite(&E,    8, 1, f);
        fwrite(&hs,   1, 1, f);
        fwrite(&gs,   1, 1, f);
        fwrite(&ps,   1, 1, f);
        fwrite(m->vocab[i].word, 1, wlen, f);
    }

    /* A edges */
    for (int i = 0; i < m->am_cap; i++) {
        if (m->am[i].key == 0 || m->am[i].val < min_weight) continue;
        uint32_t ai = m->am[i].key >> 15;
        uint32_t aj = m->am[i].key & 0x7FFF;
        double   aw = m->am[i].val;
        fwrite(&ai, 4, 1, f);
        fwrite(&aj, 4, 1, f);
        fwrite(&aw, 8, 1, f);
    }

    fclose(f);
    fprintf(stderr, "[checkpoint] saved %s  vocab=%d  A=%d\n",
            path, vocab_count, a_count);
    return 0;
}
