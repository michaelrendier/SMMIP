/*
 * PtolC/filter.c — Learn-time token filter and filetype dispatch.
 *
 * token_accept() is called for every token before it is assigned a
 * Riemann zero slot.  Rejection is silent at the caller level (the
 * count is tracked in Monad.rejected_count) and never fatal.
 *
 * To add a new filetype:
 *   1. Add a NS_FT_* constant to filter.h
 *   2. Add a row to FILETYPE_RULES below
 *   3. Add extension mappings in filetype_from_ext()
 *   That is all.
 */

#include <string.h>
#include <strings.h>
#include <stddef.h>
#include "filter.h"

/* ── Filetype rule table ──────────────────────────────────────────────────── */
/*                       ft           max  hex   /    dig  caps  b64  vowel */
static const FTRules FILETYPE_RULES[] = {
    { NS_FT_PROSE,    24,  0,    0,    0,    0,    16,  1  },
    { NS_FT_CODE,     40,  0,    0,    1,    1,    0,   0  },
    { NS_FT_MARKUP,   24,  0,    0,    0,    0,    16,  1  },
    { NS_FT_DOC,      24,  0,    0,    0,    0,    16,  1  },
};
#define N_FT_RULES ((int)(sizeof(FILETYPE_RULES)/sizeof(FILETYPE_RULES[0])))

static const FTRules *rules_for(NSFiletype ft)
{
    for (int i = 0; i < N_FT_RULES; i++)
        if (FILETYPE_RULES[i].ft == ft) return &FILETYPE_RULES[i];
    return &FILETYPE_RULES[0];   /* default: prose rules */
}

/* ── Extension → filetype mapping ────────────────────────────────────────── */

NSFiletype filetype_from_ext(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return NS_FT_PROSE;

    /* Case-insensitive compare helper */
    #define EXT(s) (strcasecmp(dot, (s)) == 0)

    if (EXT(".c")   || EXT(".h")   || EXT(".cpp") || EXT(".hpp") ||
        EXT(".cc")  || EXT(".cxx") || EXT(".py")  || EXT(".rb")  ||
        EXT(".sh")  || EXT(".bash")|| EXT(".zsh") || EXT(".go")  ||
        EXT(".rs")  || EXT(".java")|| EXT(".js")  || EXT(".ts")  ||
        EXT(".pl")  || EXT(".lua") || EXT(".r"))
        return NS_FT_CODE;

    if (EXT(".html") || EXT(".htm") || EXT(".xml") || EXT(".svg"))
        return NS_FT_MARKUP;

    if (EXT(".pdf") || EXT(".PDF") ||
        EXT(".doc") || EXT(".DOC") || EXT(".docx") || EXT(".DOCX") ||
        EXT(".odt") || EXT(".ODT") || EXT(".rtf")  || EXT(".RTF"))
        return NS_FT_DOC;

    #undef EXT
    return NS_FT_PROSE;   /* .txt .md .rst .org .tex .bib etc. */
}

const char *filetype_name(NSFiletype ft)
{
    switch (ft) {
        case NS_FT_PROSE:  return "prose";
        case NS_FT_CODE:   return "code";
        case NS_FT_MARKUP: return "markup";
        case NS_FT_DOC:    return "doc";
        default:           return "prose";
    }
}

/* ── Token classifier helpers ─────────────────────────────────────────────── */

static int is_pure_numeric(const char *s)
{
    if (!*s) return 0;
    for (; *s; s++)
        if (*s < '0' || *s > '9') return 0;
    return 1;
}

static int is_hex_string(const char *s, size_t len)
{
    /* 0x... prefix */
    if (len >= 3 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) return 1;
    /* All chars are hex digits and length suggests encoded data */
    if (len < 6) return 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) return 0;
    }
    return 1;
}

/* UUID: 8-4-4-4-12 hex with dashes, total length 36 */
static int is_uuid(const char *s, size_t len)
{
    if (len != 36) return 0;
    static const int dash_pos[] = {8, 13, 18, 23, -1};
    for (int i = 0; i < 36; i++) {
        int is_dash = 0;
        for (int d = 0; dash_pos[d] >= 0; d++)
            if (dash_pos[d] == i) { is_dash = 1; break; }
        if (is_dash) { if (s[i] != '-') return 0; continue; }
        char c = s[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) return 0;
    }
    return 1;
}

/* base64: ≥ b64_min_len chars, ≥95% base64 alphabet, ends with = */
static int is_base64_chunk(const char *s, size_t len, int min_len)
{
    if ((int)len < min_len) return 0;
    if (s[len - 1] != '=') return 0;
    int ok = 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=')
            ok++;
    }
    return ok * 100 / (int)len >= 95;
}

static int vowel_count(const char *s)
{
    int n = 0;
    for (; *s; s++) {
        char c = *s | 0x20;  /* to lower */
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') n++;
    }
    return n;
}

static int high_digit_ratio(const char *s, size_t len)
{
    if (!len) return 0;
    int d = 0;
    for (size_t i = 0; i < len; i++)
        if (s[i] >= '0' && s[i] <= '9') d++;
    return d * 100 / (int)len > 50;
}

/* ALL_CAPS_WITH_UNDERSCORES longer than 6 chars — config key pattern */
static int is_long_allcaps(const char *s, size_t len)
{
    if (len <= 6) return 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == '_') continue;
        if (c >= 'A' && c <= 'Z') continue;
        if (c >= '0' && c <= '9') continue;
        return 0;
    }
    return 1;
}

/* ── Public filter ────────────────────────────────────────────────────────── */

int token_accept(const char *tok, NSFiletype ft)
{
    size_t len = strlen(tok);
    const FTRules *r = rules_for(ft);

    /* ── Universal gates (all filetypes) ─────────────────────────────────── */

    if (len < 2)                          return 0;  /* single char          */
    if ((int)len > r->max_len)            return 0;  /* too long             */
    if (is_pure_numeric(tok))             return 0;  /* 12345                */
    if (is_uuid(tok, len))                return 0;  /* UUID                 */
    if (strchr(tok, '='))                 return 0;  /* key=value            */
    if (strstr(tok, "://"))               return 0;  /* URL fragment         */

    /* ── Filetype-conditional gates ──────────────────────────────────────── */

    if (!r->allow_hex && is_hex_string(tok, len))           return 0;
    if (!r->allow_slash && strchr(tok, '/'))                return 0;
    if (!r->allow_slash && strchr(tok, '\\'))               return 0;
    if (!r->allow_high_dig && high_digit_ratio(tok, len))   return 0;
    if (!r->allow_long_caps && is_long_allcaps(tok, len))   return 0;
    if (r->b64_min_len > 0 && is_base64_chunk(tok, len, r->b64_min_len))
        return 0;

    /* ── Vowel gate (prose/markup/doc) ──────────────────────────────────────── */

    if (r->require_vowel) {
        int vc = vowel_count(tok);
        if (vc == 0)                                         return 0;
        /* Very low vowel ratio in longer tokens: "wlvfe", "schzr", etc. */
        if ((int)len >= 6 && vc * 100 / (int)len < 15)      return 0;
    }

    /* Trailing apostrophe fragment: "pins'", "ban'" */
    if (tok[len - 1] == '\'')                               return 0;

    return 1;
}
