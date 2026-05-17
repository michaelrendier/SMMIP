/*
 * PtolC/main.c — ptolemy command-line binary.
 *
 * Primary flags:
 *   -l <file|url|->   learn from file, URL (via curl), or stdin
 *   -h <prompt>       hear → speak
 *   -s                status  (or speak math if verbose >= 1)
 *   -q <word>         lookup a single word
 *   -V                version
 *
 * Verbosity modifier (stackable, combinable with primary flags):
 *   -v  / --verbose   level 1: show mathematics (β, J^μ, A edges)
 *   -lv / -hv / -sv   primary + level 1 verbose
 *   -vv               level 2: + ANSI colour (hear=cyan, speak=green, learn=yellow)
 *   -vvv              level 3: full pipeline — for -h shows learn+hear+speak
 *
 * Other flags:
 *   -c <path>         checkpoint path (overrides env/auto-search)
 *   -n                no-save: do not write checkpoint after -l
 *
 * Checkpoint search order:
 *   1. -c flag
 *   2. $PTOLEMY_CHECKPOINT env var
 *   3. ./monad_wordnet.bin
 *   4. ../Callimachus/data/monad_wordnet.bin
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "ptolemy.h"
#include "monad.h"
#include "checkpoint.h"
#include "ingest.h"
#include "daemon.h"
#include "log.h"

/* g_color and g_self_ref are defined in monad.c — set here by main() */

#define PTOLEMY_VERSION "1.115"

/* ── Checkpoint evaluator ─────────────────────────────────────────────────── */

static void run_eval(const char *ckpt_path)
{
    /* Locate eval_checkpoint.py: SMMIP_REPO env → default path → skip */
    char script[4096] = {0};
    const char *smmip = getenv("SMMIP_REPO");
    if (smmip && smmip[0]) {
        snprintf(script, sizeof(script), "%s/tools/eval_checkpoint.py", smmip);
    } else {
        const char *home = getenv("HOME");
        if (home && home[0])
            snprintf(script, sizeof(script),
                     "%s/Projects/Ptol/SMMIP/tools/eval_checkpoint.py", home);
    }
    if (!script[0]) return;

    FILE *t = fopen(script, "r");
    if (!t) return;
    fclose(t);

    /* Write JSON assessment alongside checkpoint */
    char json_out[4096];
    snprintf(json_out, sizeof(json_out), "%s.assessment.json", ckpt_path);

    char cmd[8192];
    snprintf(cmd, sizeof(cmd),
             "python3 '%s' '%s' --out '%s'", script, ckpt_path, json_out);

    fprintf(stderr, "\n[eval] running checkpoint assessment...\n");
    int rc = system(cmd);
    if (rc == 0)
        fprintf(stderr, "[eval] assessment written → %s\n", json_out);
    else if (rc > 0)
        fprintf(stderr, "[eval] verdict: WARN/FAIL (exit %d) — see %s\n", rc, json_out);
}

/* ── Ptolemy home directory ───────────────────────────────────────────────── */

static char g_ptolemy_dir[4096] = {0};

static void init_ptolemy_dir(void)
{
    const char *override = getenv("PTOLEMY_HOME");
    if (override && override[0]) {
        snprintf(g_ptolemy_dir, sizeof(g_ptolemy_dir), "%s", override);
        return;
    }
    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        struct passwd *pw = getpwuid(getuid());
        if (pw && pw->pw_dir) home = pw->pw_dir;
    }
    if (home && home[0])
        snprintf(g_ptolemy_dir, sizeof(g_ptolemy_dir), "%s/.ptolemy", home);
    else
        snprintf(g_ptolemy_dir, sizeof(g_ptolemy_dir), ".ptolemy");

    /* create ~/.ptolemy/ if it does not exist */
    mkdir(g_ptolemy_dir, 0700);
}

/* ── Arg parsing ──────────────────────────────────────────────────────────── */

typedef struct {
    char primary;   /* 'l','h','s','q','c','n','V', 0=none */
    int  v;         /* verbose count (number of 'v' chars) */
} Arg;

static Arg parse_arg(const char *a)
{
    Arg r = {0, 0};
    if (!a || a[0] != '-') return r;
    if (a[1] == '-') {
        if (strcmp(a+2, "verbose") == 0) r.v = 1;
        return r;
    }
    for (const char *p = a + 1; *p; p++) {
        switch (*p) {
            case 'v': r.v++; break;
            case 'l': case 'h': case 's': case 'w':
            case 'c': case 'n': case 'V': case 'i':
            case 'I': case 'q': case 'd': case 'D':
            case 'F': case 'S': case 'W':
                r.primary = *p; break;
            default: break;
        }
    }
    return r;
}

/* ── I/O helpers ──────────────────────────────────────────────────────────── */

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[ptolemy] cannot open %s: %s\n", path, strerror(errno));
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, sz, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

static char *read_stdin(void)
{
    size_t cap = 65536, len = 0;
    char  *buf = malloc(cap);
    int    c;
    while ((c = fgetc(stdin)) != EOF) {
        if (len + 2 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    return buf;
}

static char *read_url(const char *url)
{
    for (const char *p = url; *p; p++) {
        if (*p == '\'') {
            fprintf(stderr, "[ptolemy] URL contains single quote — refused\n");
            return NULL;
        }
    }
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "curl -sf '%s'", url);
    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        fprintf(stderr, "[ptolemy] curl failed for %s\n", url);
        return NULL;
    }
    size_t cap = 131072, len = 0;
    char  *buf = malloc(cap);
    int    c;
    while ((c = fgetc(pipe)) != EOF) {
        if (len + 2 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    pclose(pipe);
    return buf;
}

static const char *find_checkpoint(const char *flag_path)
{
    static char found[4096];
    static char home_ckpt[4096];

    if (g_ptolemy_dir[0])
        snprintf(home_ckpt, sizeof(home_ckpt),
                 "%s/monad_wordnet.bin", g_ptolemy_dir);

    const char *env_path = getenv("PTOLEMY_CHECKPOINT");
    const char *cands[]  = {
        flag_path,
        env_path,
        home_ckpt[0] ? home_ckpt : NULL,
        "monad_wordnet.bin",
        NULL
    };
    for (int i = 0; i < (int)(sizeof(cands)/sizeof(cands[0])) - 1; i++) {
        if (!cands[i] || !cands[i][0]) continue;
        FILE *t = fopen(cands[i], "rb");
        if (t) {
            fclose(t);
            strncpy(found, cands[i], sizeof(found) - 1);
            found[sizeof(found) - 1] = '\0';
            return found;
        }
    }
    return NULL;
}

static void print_version(void)
{
    printf("ptolemy %s — RedBlue Geometries Engine\n", PTOLEMY_VERSION);
    printf("  Riemann zeros   N=%d\n", MONAD_N_DEFAULT);
    printf("  σ = ½           Noether forcing: J(σ,E)=0 iff σ=½\n");
    printf("  β_sat = %.3f    L_ground = %.3f\n", MONAD_BETA_SAT, MONAD_L_GROUND);
    printf("  φ = %.16f\n", MONAD_PHI);
    printf("  D* = %.5f       Ω_ζΣ = %.5f\n", MONAD_D_STAR, MONAD_OMEGA_ZS);
    printf("  Self-referential: -vvv pipes verbose → learn()\n");
    printf("  Identity:         --identity / -i  (run once after corpus)\n");
    printf("  CLAUDE-SMMNIP-00729-56714-24600\n");
}

static void print_usage(void)
{
    fprintf(stderr,
        "ptolemy %s — RedBlue Geometries Engine\n\n"
        "  -l <file|url|->  learn from file, URL, or stdin\n"
        "  -I <path>        ingest directory or file (Native Space whitelist)\n"
        "  -h <prompt>      hear → Noether response  (real J: β×E²)\n"
        "  -W <prompt>      hear → Wick-rotated response  (imaginary J: β×E²×sin(E/2))\n"
        "  -D <query>       send query to running daemon\n"
        "  -d               start daemon (keeps monad resident in memory)\n"
        "  -s               status  (or spontaneous speak if verbose)\n"
        "  -F               field health report\n"
        "  -w <word>        lookup: zero, σ, E, β, home/gen stratum\n"
        "  -i / --identity  learn Ptolemy's identity text (run once)\n"
        "  -V               version\n\n"
        "Verbosity (stackable, combinable):\n"
        "  -v / --verbose   level 1: β deepening, J^μ propagation, A edges\n"
        "  -vv              level 2: level 1 + ANSI colour\n"
        "  -vvv             level 3: full pipeline + self-referential loop\n"
        "                   (verbose output is fed back through learn())\n"
        "  -lv  -hv  -sv    combined primary + verbose\n\n"
        "Other:\n"
        "  -c <path>        checkpoint path\n"
        "  -S <path>        socket path for daemon (default: ~/.ptolemy/ptolemy.sock)\n"
        "  -n               no-save after -l\n"
        "  -q / --quiet     suppress terminal output\n\n"
        "Checkpoint: -c → $PTOLEMY_CHECKPOINT → ~/.ptolemy/monad_wordnet.bin"
        " → ./monad_wordnet.bin\n",
        PTOLEMY_VERSION);
}

/* ── Main ─────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    if (argc < 2) { print_usage(); return 1; }

    init_ptolemy_dir();

    /* ── Pre-scan: verbose level, checkpoint path, no-save, quiet ───────── */
    int         verbose    = 0;
    const char *ckpt_flag  = NULL;
    const char *sock_flag  = NULL;
    int         no_save    = 0;
    int         quiet      = 0;

    for (int i = 1; i < argc; i++) {
        if (!argv[i] || argv[i][0] != '-') continue;
        if (strcmp(argv[i], "--verbose") == 0) {
            if (verbose < 1) verbose = 1;
            continue;
        }
        if (strcmp(argv[i], "--quiet") == 0) { quiet = 1; continue; }
        Arg a = parse_arg(argv[i]);
        if (a.v > verbose) verbose = a.v;
        if (a.primary == 'c' && i + 1 < argc) ckpt_flag = argv[++i];
        if (a.primary == 'S' && i + 1 < argc) sock_flag  = argv[++i];
        if (a.primary == 'n') no_save = 1;
        if (a.primary == 'q') quiet = 1;
    }

    /* ── Colour and self-referential mode ──────────────────────────────── */
    g_color    = (verbose >= 2) && isatty(fileno(stderr)) && isatty(fileno(stdout));
    g_self_ref = (verbose >= 3);

    /* ── Logging ────────────────────────────────────────────────────────── */
    plog_init(g_ptolemy_dir, quiet);

    /* ── Load monad ─────────────────────────────────────────────────────── */
    const char *ckpt_path = find_checkpoint(ckpt_flag);
    Monad *m = monad_create(MONAD_N_DEFAULT);
    monad_ground_init(m);

    if (ckpt_path) {
        checkpoint_load(m, ckpt_path);
    } else {
        if (!quiet)
            fprintf(stderr,
                "[ptolemy] no checkpoint found — ground state  σ=0\n"
                "          place monad_wordnet.bin in ~/.ptolemy/\n");
    }

    /* ── Process arguments in order ─────────────────────────────────────── */
    int learned = 0;
    int did_ingest = 0;

    for (int i = 1; i < argc; i++) {
        if (!argv[i] || argv[i][0] != '-') continue;
        if (strcmp(argv[i], "--verbose") == 0) continue;

        Arg a = parse_arg(argv[i]);

        /* -V : version */
        if (a.primary == 'V') {
            print_version();
            continue;
        }

        /* -c / -n : handled in pre-scan */
        if (a.primary == 'c') { i++; continue; }
        if (a.primary == 'n') continue;

        /* -i / --identity : learn Ptolemy's identity ────────────────── */
        if (a.primary == 'i' ||
            (argv[i][0]=='-' && argv[i][1]=='-' && strcmp(argv[i]+2,"identity")==0)) {
            monad_learn_identity(m);
            monad_self_flush(m);
            learned = 1;
            continue;
        }

        /* -l : learn ─────────────────────────────────────────────────── */
        if (a.primary == 'l' && i + 1 < argc) {
            const char *src  = argv[++i];
            char       *text = NULL;

            if (strcmp(src, "-") == 0) {
                text = read_stdin();
            } else if (strncmp(src, "http://",  7) == 0 ||
                       strncmp(src, "https://", 8) == 0) {
                fprintf(stderr, "[ptolemy] fetching %s ...\n", src);
                text = read_url(src);
            } else {
                text = read_file(src);
            }

            if (text) {
                int lv = (verbose >= 1) ? verbose : a.v;
                fprintf(stderr, "[ptolemy] learning %s  (%zu bytes)\n",
                        src, strlen(text));
                monad_learn(m, text, lv);
                monad_self_flush(m);
                free(text);
                learned = 1;
                if (lv == 0) monad_status(m, stderr);
            }
            continue;
        }

        /* -I : filesystem ingest (Native Space whitelist) ───────────────── */
        if (a.primary == 'I' && i + 1 < argc) {
            const char *root = argv[++i];
            int iv = (verbose >= 1) ? verbose : a.v;
            const char *save_to = ckpt_path ? ckpt_path
                                : (g_ptolemy_dir[0] ? g_ptolemy_dir : NULL);
            /* Build the save path string if we have a dir but no ckpt_flag */
            static char ingest_ckpt[4096];
            if (!ckpt_path && g_ptolemy_dir[0]) {
                snprintf(ingest_ckpt, sizeof(ingest_ckpt),
                         "%s/monad_wordnet.bin", g_ptolemy_dir);
                save_to = ingest_ckpt;
            }
            int n = ingest_path(m, root, iv, save_to);
            if (n > 0) { learned = 1; did_ingest = 1; monad_status(m, stderr); }
            continue;
        }

        /* -d : start daemon ──────────────────────────────────────────── */
        if (a.primary == 'd') {
            static char d_save[4096], d_pid[4096];
            const char *sp = daemon_sock_path(sock_flag, g_ptolemy_dir);
            const char *save_ckpt = NULL;
            const char *pid_p     = NULL;
            if (!no_save) {
                if (ckpt_path) {
                    save_ckpt = ckpt_path;
                } else if (g_ptolemy_dir[0]) {
                    snprintf(d_save, sizeof(d_save),
                             "%s/monad_wordnet.bin", g_ptolemy_dir);
                    save_ckpt = d_save;
                } else {
                    save_ckpt = "monad_wordnet.bin";
                }
            }
            if (g_ptolemy_dir[0]) {
                snprintf(d_pid, sizeof(d_pid),
                         "%s/ptolemy.pid", g_ptolemy_dir);
                pid_p = d_pid;
            }
            daemon_serve(m, sp, save_ckpt, pid_p, verbose);
            monad_destroy(m);
            plog_close();
            return 0;
        }

        /* -D : query daemon ──────────────────────────────────────────── */
        if (a.primary == 'D' && i + 1 < argc) {
            const char *query = argv[++i];
            const char *sp    = daemon_sock_path(sock_flag, g_ptolemy_dir);
            int rc = daemon_query(query, sp);
            monad_destroy(m);
            plog_close();
            return rc == 0 ? 0 : 1;
        }

        /* -F : field health ──────────────────────────────────────────── */
        if (a.primary == 'F') {
            monad_health(m, stdout);
            continue;
        }

        /* -S : socket path (handled in pre-scan, skip here) ─────────── */
        if (a.primary == 'S') { i++; continue; }

        /* -W : hear → Wick-rotated speak (σ → iσ, imaginary J) ─────────── */
        if (a.primary == 'W' && i + 1 < argc) {
            const char *query = argv[++i];
            int wv = (verbose >= 1) ? verbose : a.v;
            char *response = monad_speak_wick(m, query, 50, wv);
            printf("%s\n", response);
            free(response);
            continue;
        }

        /* -h : hear → speak ──────────────────────────────────────────── */
        if (a.primary == 'h' && i + 1 < argc) {
            const char *query = argv[++i];
            int hv = (verbose >= 1) ? verbose : a.v;

            if (hv == 0) {
                char  qcopy[4096];
                char *tok;
                strncpy(qcopy, query, sizeof(qcopy) - 1);
                tok = strtok(qcopy, " \t\r\n");
                while (tok) {
                    monad_lookup(m, tok, stderr);
                    tok = strtok(NULL, " \t\r\n");
                }
            }

            char *response = monad_speak(m, query, 50, hv);
            printf("%s\n", response);
            free(response);
            monad_self_flush(m);
            if (g_self_ref) learned = 1;  /* save after self-ref cycle */
            continue;
        }

        /* -s : status / verbose speak ────────────────────────────────── */
        if (a.primary == 's') {
            int sv = (verbose >= 1) ? verbose : a.v;
            if (sv >= 1) {
                char *response = monad_speak(m, "", 50, sv);
                printf("%s\n", response);
                free(response);
                monad_self_flush(m);
                if (g_self_ref) learned = 1;
            }
            monad_status(m, stdout);
            continue;
        }

        /* -w : word lookup ───────────────────────────────────────────── */
        if (a.primary == 'w' && i + 1 < argc) {
            monad_lookup(m, argv[++i], stdout);
            continue;
        }

        /* -q : quiet (handled in pre-scan, skip here) ────────────────── */
        if (a.primary == 'q') continue;

        /* Bare -v / -vv / -vvv with no primary: spontaneous speak */
        if (a.primary == 0 && a.v > 0) {
            int bv = a.v > verbose ? a.v : verbose;
            char *response = monad_speak(m, "", 50, bv);
            printf("%s\n", response);
            free(response);
            monad_self_flush(m);
            if (g_self_ref) learned = 1;
            continue;
        }

        if (a.primary != 0)
            fprintf(stderr, "[ptolemy] flag -%c: missing argument\n", a.primary);
    }

    /* ── Save if learned ─────────────────────────────────────────────────── */
    if (learned && !no_save) {
        static char default_save[4096];
        const char *save;
        if (ckpt_path) {
            save = ckpt_path;
        } else if (g_ptolemy_dir[0]) {
            snprintf(default_save, sizeof(default_save),
                     "%s/monad_wordnet.bin", g_ptolemy_dir);
            save = default_save;
        } else {
            save = "monad_wordnet.bin";
        }
        checkpoint_save(m, save, 0.0);
        if (did_ingest && !quiet)
            run_eval(save);
    }

    monad_destroy(m);
    plog_close();
    return 0;
}
