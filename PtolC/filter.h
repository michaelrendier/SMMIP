/*
 * PtolC/filter.h — Learn-time token filter and filetype dispatch.
 *
 * Every token processed by monad_learn_ex() passes through token_accept()
 * before being assigned a Riemann zero slot.  Rejection is never fatal:
 * the token is counted and skipped; the rest of the text continues.
 *
 * Each filetype carries its own acceptance rules in a FTRules entry.
 * Adding a new filetype = adding one row to the FILETYPE_RULES table
 * in filter.c.  No other code changes required.
 *
 * Native Space principle: a token earns a zero slot only if it can be
 * assigned a stratum address in the Dixon tower.  Tokens that are purely
 * syntactic (numbers, hex, UUIDs, paths, base64) are Cartesian noise and
 * are rejected regardless of filetype.
 */

#ifndef FILTER_H
#define FILTER_H

#include <stddef.h>

/* ── Filetype registry ────────────────────────────────────────────────────── */

typedef enum {
    NS_FT_PROSE   = 0,  /* plain text, markdown, RST, LaTeX, BibTeX */
    NS_FT_CODE    = 1,  /* source code — identifiers + comments     */
    NS_FT_MARKUP  = 2,  /* HTML/XML text nodes (post-extraction)     */
    NS_FT_DOC     = 3,  /* PDF/DOCX/ODT/RTF prose output            */
    NS_FT_WORDNET = 4,  /* WordNet/dictionary corpus — canonical English */
    NS_FT_AUTO    = -1  /* fall back to NS_FT_PROSE rules           */
} NSFiletype;

/* Per-filetype acceptance rules — one row per filetype in FILETYPE_RULES. */
typedef struct {
    NSFiletype  ft;
    int         max_len;        /* reject tokens longer than this          */
    int         allow_hex;      /* allow pure-hex tokens (e.g. 0xDEAD)     */
    int         allow_slash;    /* allow tokens containing /               */
    int         allow_high_dig; /* allow tokens where >50% chars are digits*/
    int         allow_long_caps;/* allow ALL_CAPS tokens > 6 chars         */
    int         b64_min_len;    /* base64 check threshold (0 = skip check) */
    int         require_vowel;  /* reject tokens with no a/e/i/o/u        */
} FTRules;

/**
 * Resolve file extension to a filetype.
 *
 * :param path: File path (only the extension is examined).
 * :returns:    NSFiletype value.
 */
NSFiletype filetype_from_ext(const char *path);

/**
 * Return a human-readable name for a filetype (for logging).
 *
 * :param ft: NSFiletype value.
 * :returns:  Static string.
 */
const char *filetype_name(NSFiletype ft);

/**
 * Assess whether a token is acceptable for the given filetype.
 *
 * :param tok: NUL-terminated token (lowercase, already normalised).
 * :param ft:  Filetype of the source document.
 * :returns:   1 if the token should be ingested, 0 if it should be rejected.
 */
int token_accept(const char *tok, NSFiletype ft);

#endif /* FILTER_H */
