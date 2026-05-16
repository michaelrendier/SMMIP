/*
 * PtolC/tools/checkpoint_expand.c — grow a ptolemy checkpoint by adding zeros.
 *
 * Usage:
 *   gcc -O2 -o checkpoint_expand checkpoint_expand.c -lm
 *   ./checkpoint_expand monad_wordnet.bin 270760
 *
 * Reads the existing checkpoint, appends (N_new - N_old) zeros at ground VEV,
 * preserves all existing beta/age/vocab/A data exactly.
 * Writes atomically via temp file + rename.
 *
 * The existing field is unaffected:
 *   - beta[0..N_old-1]:  unchanged
 *   - age[0..N_old-1]:   unchanged
 *   - vocab, A entries:  unchanged (idx values remain valid)
 *   - New zeros:         beta = |L_GROUND| / N_new, age = 0
 *
 * After expansion, word_coords() uses N_new for all new words.
 * Old vocab entries keep their original idx (still valid in [0, N_old) c [0, N_new)).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#define CKPT_MAGIC   "PTOL"
#define L_GROUND     (-1.888)

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr,
            "Usage: checkpoint_expand <checkpoint.bin> <N_new>\n"
            "\n"
            "  Expands the zero field from current N to N_new.\n"
            "  N_new must be greater than the current N.\n"
            "  Existing field data is preserved exactly.\n"
            "  New zeros initialised at ground VEV = |L_GROUND| / N_new.\n"
            "\n"
            "  Batches of 512:  N_new = current_N + 512 * num_batches\n"
            "  Example (450 batches from N=25000):\n"
            "    ./checkpoint_expand monad_wordnet.bin 255400\n");
        return 1;
    }

    const char *path = argv[1];
    int N_new = atoi(argv[2]);
    if (N_new <= 0) {
        fprintf(stderr, "N_new must be a positive integer\n");
        return 1;
    }

    /* ── Open and validate ─────────────────────────────────────────────────── */
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, CKPT_MAGIC, 4) != 0) {
        fprintf(stderr, "Not a ptolemy checkpoint: %s\n", path);
        fclose(f); return 1;
    }

    uint32_t version, N_old, vocab_size, A_size, wc;
    double   threshold;
    if (fread(&version,    4, 1, f) != 1 ||
        fread(&N_old,      4, 1, f) != 1 ||
        fread(&vocab_size, 4, 1, f) != 1 ||
        fread(&A_size,     4, 1, f) != 1 ||
        fread(&wc,         4, 1, f) != 1 ||
        fread(&threshold,  8, 1, f) != 1) {
        fprintf(stderr, "Truncated header in %s\n", path);
        fclose(f); return 1;
    }

    if ((int)N_old >= N_new) {
        fprintf(stderr, "N_new (%d) must be greater than current N (%u)\n",
                N_new, N_old);
        fclose(f); return 1;
    }

    int N_add = N_new - (int)N_old;
    double beta0_old = fabs(L_GROUND) / N_old;
    double beta0_new = fabs(L_GROUND) / N_new;

    printf("checkpoint_expand:\n");
    printf("  file:         %s\n", path);
    printf("  N:            %u → %d  (+%d zeros, +%d batches of 512)\n",
           N_old, N_new, N_add, N_add / 512);
    printf("  ground VEV:   %.6e → %.6e\n", beta0_old, beta0_new);
    printf("  version:      %u\n", version);
    printf("  vocab:        %u entries (unchanged)\n", vocab_size);
    printf("  A edges:      %u (unchanged)\n", A_size);
    printf("  word count:   %u (unchanged)\n", wc);
    printf("\n");

    /* ── Read existing beta and age ────────────────────────────────────────── */
    double   *beta = malloc(N_old * sizeof(double));
    int32_t  *age  = malloc(N_old * sizeof(int32_t));
    if (!beta || !age) { fprintf(stderr, "out of memory\n"); return 1; }

    if (fread(beta, sizeof(double),  N_old, f) != N_old ||
        fread(age,  sizeof(int32_t), N_old, f) != N_old) {
        fprintf(stderr, "Read error on beta/age\n");
        fclose(f); return 1;
    }

    /* ── Read remaining data (vocab + A) ───────────────────────────────────── */
    long tail_start = ftell(f);
    fseek(f, 0, SEEK_END);
    long tail_size  = ftell(f) - tail_start;
    fseek(f, tail_start, SEEK_SET);

    char *tail = malloc(tail_size > 0 ? tail_size : 1);
    if (!tail) { fprintf(stderr, "out of memory\n"); return 1; }
    if (tail_size > 0 && fread(tail, 1, tail_size, f) != (size_t)tail_size) {
        fprintf(stderr, "Read error on vocab/A\n");
        fclose(f); return 1;
    }
    fclose(f);

    /* ── Write expanded checkpoint ─────────────────────────────────────────── */
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s.expand_tmp", path);
    FILE *out = fopen(tmp, "wb");
    if (!out) { perror(tmp); return 1; }

    /* Header — N_new replaces N_old, everything else identical */
    uint32_t N_new_u = (uint32_t)N_new;
    fwrite(CKPT_MAGIC, 4, 1, out);
    fwrite(&version,    4, 1, out);
    fwrite(&N_new_u,    4, 1, out);
    fwrite(&vocab_size, 4, 1, out);
    fwrite(&A_size,     4, 1, out);
    fwrite(&wc,         4, 1, out);
    fwrite(&threshold,  8, 1, out);

    /* Existing beta (unchanged) */
    fwrite(beta, sizeof(double), N_old, out);
    /* New zeros at ground VEV */
    for (int i = 0; i < N_add; i++)
        fwrite(&beta0_new, sizeof(double), 1, out);

    /* Existing age (unchanged) */
    fwrite(age, sizeof(int32_t), N_old, out);
    /* New zeros at age 0 */
    int32_t zero_age = 0;
    for (int i = 0; i < N_add; i++)
        fwrite(&zero_age, sizeof(int32_t), 1, out);

    /* Vocab + A — completely unchanged */
    if (tail_size > 0)
        fwrite(tail, 1, tail_size, out);

    fclose(out);

    /* Atomic rename */
    if (rename(tmp, path) != 0) {
        perror("rename");
        fprintf(stderr, "Expanded checkpoint is at: %s\n", tmp);
        return 1;
    }

    printf("  Done. Checkpoint expanded in-place.\n");
    printf("  New size: beta(%d×8) + age(%d×4) + tail(%ld)\n",
           N_new, N_new, tail_size);
    printf("\n");
    printf("  Next: ./ptolemy -s   (verify N=%d)\n", N_new);

    free(beta); free(age); free(tail);
    return 0;
}
