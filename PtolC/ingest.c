/*
 * PtolC/ingest.c — Filesystem ingest for the C Monad.
 *
 * Extractor dispatch:
 *   .pdf              → pdftotext -q
 *   .doc / .DOC       → catdoc
 *   .docx/.odt/.rtf   → pandoc -t plain
 *   .html/.htm        → libxml2 htmlReadFile (fallback: plain text)
 *   .xml/.svg         → libxml2 xmlReadFile  (fallback: plain text)
 *   everything else   → plain UTF-8 read
 *
 * .ptolemyignore:
 *   Each directory is checked for a .ptolemyignore file.  Lines are
 *   fnmatch() patterns applied to filenames (not full paths).
 *   Lines beginning with # are comments.  Patterns apply to the
 *   immediate directory only (cascade behaviour is Phase 4).
 *
 * Extension whitelist enforces Native Space addressability: only files
 * that carry semantic content are ingested.  Config, binary, and data
 * files are excluded by design.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <fnmatch.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

#ifdef HAVE_LIBXML2
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#endif

#include "monad.h"
#include "filter.h"
#include "checkpoint.h"
#include "log.h"
#include "ingest.h"

/* ── Extension tables ─────────────────────────────────────────────────────── */

/* Full whitelist — extensions that carry Native Space addressable content. */
static const char *const WHITELIST[] = {
    /* Plain prose */
    ".txt", ".text", ".md", ".rst", ".org", ".tex", ".bib",
    /* Markup — extracted via libxml2 or plain-text fallback */
    ".html", ".htm", ".xml", ".svg",
    /* Rich documents — extracted via subprocess */
    ".pdf", ".PDF",
    ".doc", ".DOC", ".docx", ".DOCX",
    ".odt", ".ODT", ".rtf", ".RTF",
    /* Code with semantic comments and identifiers */
    ".c", ".h", ".cpp", ".hpp", ".cc", ".cxx",
    ".py", ".rb", ".sh", ".bash", ".zsh",
    ".go", ".rs", ".java", ".js", ".ts",
    ".pl", ".lua", ".r",
    NULL
};

/* Directory names that never contain Native Space addressable content.
 * Key/credential directories are listed explicitly as defence-in-depth —
 * the whitelist already blocks key file extensions, but these dirs should
 * never be traversed at all. */
static const char *const PRUNE_NAMES[] = {
    /* System pseudo-filesystems */
    "proc", "sys", "dev", "run", "tmp",
    /* User noise */
    ".steam", ".wine", ".cache",
    /* Version control / build */
    "__pycache__", ".git", ".svn", ".hg", "node_modules",
    /* Cryptographic key and credential directories — off limits */
    ".ssh", ".gnupg", ".gpg",
    ".aws", ".azure", ".gcloud",
    "keyrings", ".cert", ".certs", ".pki",
    NULL
};

/* ── .ptolemyignore ───────────────────────────────────────────────────────── */

#define IGNORE_MAX 64
#define IGNORE_LEN 512

typedef struct {
    char pats[IGNORE_MAX][IGNORE_LEN];
    int  npats;
} IgnoreSet;

static void ignore_load(const char *dir, IgnoreSet *ig)
{
    ig->npats = 0;
    char path[4096];
    snprintf(path, sizeof(path), "%s/.ptolemyignore", dir);

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[IGNORE_LEN];
    while (fgets(line, sizeof(line), f) && ig->npats < IGNORE_MAX) {
        /* strip trailing newline */
        size_t l = strlen(line);
        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r'))
            line[--l] = '\0';
        if (l == 0 || line[0] == '#') continue;
        memcpy(ig->pats[ig->npats], line, l < IGNORE_LEN - 1 ? l : IGNORE_LEN - 2);
        ig->pats[ig->npats][l < IGNORE_LEN - 1 ? l : IGNORE_LEN - 2] = '\0';
        ig->npats++;
    }
    fclose(f);
}

static int ignore_match(const IgnoreSet *ig, const char *name)
{
    for (int i = 0; i < ig->npats; i++)
        if (fnmatch(ig->pats[i], name, FNM_PERIOD) == 0) return 1;
    return 0;
}

/* ── Walk state ───────────────────────────────────────────────────────────── */

typedef struct {
    Monad      *m;
    int         verbose;
    const char *ckpt_path;
    int         files_done;
    int         files_skipped;
} WalkState;

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static int str_lceq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : (unsigned char)*a;
        int cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : (unsigned char)*b;
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

static int ext_ok(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path) return 0;
    for (int i = 0; WHITELIST[i]; i++)
        if (str_lceq(dot, WHITELIST[i])) return 1;
    return 0;
}

static int name_pruned(const char *name)
{
    for (int i = 0; PRUNE_NAMES[i]; i++)
        if (strcmp(name, PRUNE_NAMES[i]) == 0) return 1;
    return 0;
}

/* Read a regular file into a malloc'd buffer (caller frees). */
static char *slurp(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    if (sz <= 0 || sz > 256 * 1024 * 1024) { fclose(f); return NULL; }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t got = fread(buf, 1, (size_t)sz, f);
    buf[got] = '\0';
    fclose(f);
    if (out_len) *out_len = got;
    return buf;
}

/* Shell-safe single-quote escaping: ' → '\'' */
static char *shell_quote(const char *path)
{
    size_t n = strlen(path);
    size_t nq = 0;
    for (size_t i = 0; i < n; i++) if (path[i] == '\'') nq++;

    char *out = malloc(n + nq * 3 + 3);
    if (!out) return NULL;
    char *o = out;
    *o++ = '\'';
    for (size_t i = 0; i < n; i++) {
        if (path[i] == '\'') {
            *o++ = '\''; *o++ = '\\'; *o++ = '\''; *o++ = '\'';
        } else {
            *o++ = path[i];
        }
    }
    *o++ = '\'';
    *o   = '\0';
    return out;
}

/* Run an external command and capture stdout into a malloc'd buffer. */
static char *popen_extract(const char *cmd)
{
    FILE *pipe = popen(cmd, "r");
    if (!pipe) return NULL;

    size_t cap = 131072, len = 0;
    char  *buf = malloc(cap);
    if (!buf) { pclose(pipe); return NULL; }

    int c;
    while ((c = fgetc(pipe)) != EOF) {
        if (len + 2 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); pclose(pipe); return NULL; }
            buf = nb;
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    pclose(pipe);
    return (len > 0) ? buf : (free(buf), NULL);
}

/* ── Subprocess extractors ────────────────────────────────────────────────── */

static char *extract_pdftotext(const char *path)
{
    char *qp  = shell_quote(path);
    if (!qp) return NULL;
    char  cmd[4096 + 32];
    snprintf(cmd, sizeof(cmd), "pdftotext -q %s -", qp);
    free(qp);
    return popen_extract(cmd);
}

static char *extract_catdoc(const char *path)
{
    char *qp = shell_quote(path);
    if (!qp) return NULL;
    char cmd[4096 + 16];
    snprintf(cmd, sizeof(cmd), "catdoc %s", qp);
    free(qp);
    return popen_extract(cmd);
}

static char *extract_pandoc(const char *path)
{
    char *qp = shell_quote(path);
    if (!qp) return NULL;
    char cmd[4096 + 40];
    snprintf(cmd, sizeof(cmd), "pandoc -t plain --wrap=none %s 2>/dev/null", qp);
    free(qp);
    return popen_extract(cmd);
}

/* ── libxml2 HTML/XML extractor ───────────────────────────────────────────── */

#ifdef HAVE_LIBXML2

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} TextBuf;

static void tbuf_append(TextBuf *tb, const char *s, size_t n)
{
    if (n == 0) return;
    while (tb->len + n + 2 >= tb->cap) {
        tb->cap = tb->cap ? tb->cap * 2 : 65536;
        tb->buf = realloc(tb->buf, tb->cap);
    }
    memcpy(tb->buf + tb->len, s, n);
    tb->len += n;
    tb->buf[tb->len] = '\0';
}

static void walk_xml_nodes(xmlNode *node, TextBuf *tb, int skip)
{
    for (xmlNode *n = node; n; n = n->next) {
        int child_skip = skip;
        if (n->type == XML_ELEMENT_NODE) {
            const char *nm = (const char *)n->name;
            if (strcasecmp(nm, "script") == 0 ||
                strcasecmp(nm, "style")  == 0)
                child_skip = 1;
        }
        if (!skip && n->type == XML_TEXT_NODE && n->content) {
            const char *s = (const char *)n->content;
            size_t      l = strlen(s);
            if (l > 0) {
                tbuf_append(tb, s, l);
                tbuf_append(tb, " ", 1);
            }
        }
        if (n->children)
            walk_xml_nodes(n->children, tb, child_skip);
    }
}

static char *extract_html(const char *path)
{
    xmlDoc *doc = htmlReadFile(path, NULL,
        HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET);
    if (!doc) return slurp(path, NULL);

    TextBuf tb = {NULL, 0, 0};
    walk_xml_nodes(xmlDocGetRootElement(doc), &tb, 0);
    xmlFreeDoc(doc);
    return tb.buf;   /* may be NULL if empty */
}

static char *extract_xml(const char *path)
{
    xmlDoc *doc = xmlReadFile(path, NULL,
        XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_NONET);
    if (!doc) return slurp(path, NULL);

    TextBuf tb = {NULL, 0, 0};
    walk_xml_nodes(xmlDocGetRootElement(doc), &tb, 0);
    xmlFreeDoc(doc);
    return tb.buf;
}

#else /* HAVE_LIBXML2 not defined — fall back to plain text */

static char *extract_html(const char *path) { return slurp(path, NULL); }
static char *extract_xml(const char *path)  { return slurp(path, NULL); }

#endif /* HAVE_LIBXML2 */

/* ── Extractor dispatch ───────────────────────────────────────────────────── */

typedef char *(*ExtractFn)(const char *path);

typedef struct {
    const char *ext;
    ExtractFn   fn;
} ExtEntry;

static const ExtEntry EXTRACTORS[] = {
    {".pdf",  extract_pdftotext},
    {".PDF",  extract_pdftotext},
    {".doc",  extract_catdoc},
    {".DOC",  extract_catdoc},
    {".docx", extract_pandoc},
    {".DOCX", extract_pandoc},
    {".odt",  extract_pandoc},
    {".ODT",  extract_pandoc},
    {".rtf",  extract_pandoc},
    {".RTF",  extract_pandoc},
    {".html", extract_html},
    {".htm",  extract_html},
    {".xml",  extract_xml},
    {".svg",  extract_xml},
    {NULL, NULL}
};

/* Returns extracted text for path — caller frees.  NULL = skip. */
static char *extract(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (dot) {
        for (int i = 0; EXTRACTORS[i].ext; i++)
            if (strcmp(dot, EXTRACTORS[i].ext) == 0)
                return EXTRACTORS[i].fn(path);
    }
    return slurp(path, NULL);   /* plain text fallback */
}

/* ── Dynamic content sniffer ──────────────────────────────────────────────── *
 * Samples the first 8 KB / 100 lines of extracted text to decide whether the
 * file is worth learning from and what filter strength to apply.
 *
 * Returns:
 *   -1              skip entirely  (host blacklist, pure domain list, binary)
 *   NS_FT_CODE (1)  use code filter (high symbol density)
 *   NS_FT_PROSE(0)  override extension default with prose filter
 *   -2              use extension-based filetype (no override)               */
static int content_sniff(const char *text, size_t len)
{
    if (!text || len < 4) return -1;

    size_t sample    = len < 8192 ? len : 8192;
    int    lines     = 0;
    int    ip_lines  = 0;   /* "0.0.0.0 host" or "127.0.0.1 host" format    */
    int    short_ln  = 0;   /* lines with ≤ 2 whitespace-separated tokens    */
    size_t alpha_ch  = 0;   /* alphabetic characters                         */
    size_t nonws_ch  = 0;   /* non-whitespace characters                     */

    const char *p   = text;
    const char *end = text + sample;

    while (p < end && lines < 100) {
        const char *nl  = memchr(p, '\n', (size_t)(end - p));
        const char *le  = nl ? nl : end;

        /* IP-address host-file detection: line starts with d+.d+.d+.d+ */
        if (le - p > 7) {
            int a, b, c, d; char ch;
            if (sscanf(p, "%d.%d.%d.%d%c", &a, &b, &c, &d, &ch) >= 4 &&
                (unsigned)a < 256 && (unsigned)b < 256)
                ip_lines++;
        }

        /* Per-character stats + word count */
        int words = 0, in_tok = 0;
        for (const char *q = p; q < le; q++) {
            unsigned char uc = (unsigned char)*q;
            if (isspace(uc)) { in_tok = 0; }
            else { if (!in_tok) { in_tok = 1; words++; } nonws_ch++; }
            if (isalpha(uc)) alpha_ch++;
        }
        if (words <= 2) short_ln++;
        lines++;
        p = nl ? nl + 1 : end;
    }

    if (lines == 0) return -1;

    /* Host-file check: > 15% of lines are IP-prefixed → skip */
    if (ip_lines * 100 > lines * 15) return -1;

    /* Pure-list check: > 80% of lines are ≤ 2 tokens → skip
     * (catches bare domain lists, CSV ID columns, word lists) */
    if (short_ln * 100 > lines * 80) return -1;

    /* Symbol-heavy check: < 40% alpha → treat as code (strict filter) */
    double alpha_r = nonws_ch > 0 ? (double)alpha_ch / (double)nonws_ch : 0.0;
    if (alpha_r < 0.40) return NS_FT_CODE;

    return -2; /* use extension-based filetype */
}

/* ── Recursive walker ─────────────────────────────────────────────────────── */

static void walk_dir(const char *path, WalkState *ws)
{
    DIR *d = opendir(path);
    if (!d) return;

    /* Load .ptolemyignore for this directory */
    IgnoreSet ig;
    ignore_load(path, &ig);

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.' &&
            (ent->d_name[1] == '\0' ||
             (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
            continue;

        if (name_pruned(ent->d_name))              continue;
        if (ignore_match(&ig, ent->d_name))        continue;

        /* Build full path */
        size_t plen = strlen(path);
        size_t nlen = strlen(ent->d_name);
        char  *full = malloc(plen + nlen + 2);
        if (!full) continue;
        memcpy(full, path, plen);
        full[plen] = '/';
        memcpy(full + plen + 1, ent->d_name, nlen + 1);

        struct stat st;
        if (lstat(full, &st) != 0) { free(full); continue; }

        if (S_ISDIR(st.st_mode)) {
            walk_dir(full, ws);
        } else if (S_ISREG(st.st_mode)) {
            if (!ext_ok(full)) { ws->files_skipped++; free(full); continue; }
            if (st.st_size == 0) { free(full); continue; }

            char *text = extract(full);
            if (text) {
                int sniff = content_sniff(text, strlen(text));
                if (sniff == -1) {
                    plog(PLOG_INFO, "skip (list/noise) %s", full);
                    free(text); ws->files_skipped++; free(full); continue;
                }
                NSFiletype ft = (sniff >= 0) ? (NSFiletype)sniff
                                             : filetype_from_ext(full);
                plog(PLOG_INFO, "ingest %s  (%zu bytes)", full, strlen(text));
                monad_learn_ex(ws->m, text, ws->verbose, ft);
                monad_self_flush(ws->m);
                free(text);
                ws->files_done++;

                if (ws->ckpt_path &&
                    ws->files_done % INGEST_SAVE_EVERY == 0) {
                    plog(PLOG_INFO, "checkpoint after %d files — %s",
                         ws->files_done, ws->ckpt_path);
                    checkpoint_save(ws->m, ws->ckpt_path, 0.0);
                }
            }
        }

        free(full);
    }

    closedir(d);
}

/* ── Public API ───────────────────────────────────────────────────────────── */

int ingest_path(Monad *m, const char *root, int verbose, const char *ckpt_path)
{
#ifdef HAVE_LIBXML2
    xmlInitParser();
#endif

    struct stat st;
    if (stat(root, &st) != 0) {
        plog(PLOG_ERROR, "cannot stat %s: %s", root, strerror(errno));
        return -1;
    }

    if (S_ISREG(st.st_mode)) {
        if (!ext_ok(root)) {
            plog(PLOG_WARN, "%s — not on Native Space whitelist", root);
            return 0;
        }
        char *text = extract(root);
        if (!text) return 0;
        plog(PLOG_INFO, "ingest %s  (%zu bytes)", root, strlen(text));
        monad_learn_ex(m, text, verbose, filetype_from_ext(root));
        monad_self_flush(m);
        free(text);
        if (ckpt_path) checkpoint_save(m, ckpt_path, 0.0);
#ifdef HAVE_LIBXML2
        xmlCleanupParser();
#endif
        return 1;
    }

    if (!S_ISDIR(st.st_mode)) {
        plog(PLOG_ERROR, "%s — not a regular file or directory", root);
        return -1;
    }

    WalkState ws = { m, verbose, ckpt_path, 0, 0 };
    walk_dir(root, &ws);

    plog(PLOG_INFO, "ingest complete — files=%d  skipped=%d",
         ws.files_done, ws.files_skipped);

#ifdef HAVE_LIBXML2
    xmlCleanupParser();
#endif
    return ws.files_done;
}
