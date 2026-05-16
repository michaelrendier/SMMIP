/*
 * PtolC/log.c — Ptolemy logging implementation.
 *
 * 4-hour slot rotation: slot = (hour / 4) * 4
 *   → 0000, 0400, 0800, 1200, 1600, 2000
 *
 * Sunday 10:00 GC: scan logs/, delete files where
 *   max(st_atime, st_mtime) < now - PLOG_GC_MAX_AGE_DAYS * 86400
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "log.h"

static FILE *g_log_fp   = NULL;
static int   g_quiet    = 0;
static int   g_init_done = 0;
static char  g_log_dir[4096] = {0};

static const char *level_str(int level)
{
    switch (level) {
        case PLOG_WARN:  return "WARN ";
        case PLOG_ERROR: return "ERROR";
        default:         return "INFO ";
    }
}

/* ── GC ───────────────────────────────────────────────────────────────────── */

static void run_gc(void)
{
    if (!g_log_dir[0]) return;

    DIR *d = opendir(g_log_dir);
    if (!d) return;

    time_t threshold = time(NULL) - (time_t)PLOG_GC_MAX_AGE_DAYS * 86400;
    struct dirent *ent;

    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        /* Only touch files that look like our log names */
        if (strncmp(ent->d_name, "ptolemy_", 8) != 0) continue;

        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", g_log_dir, ent->d_name);

        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;

        time_t last = st.st_atime > st.st_mtime ? st.st_atime : st.st_mtime;
        if (last < threshold) {
            remove(full);
            /* Log the GC action after the fact — g_log_fp already open */
            if (g_log_fp)
                fprintf(g_log_fp, "[GC] removed %s\n", ent->d_name);
        }
    }
    closedir(d);
}

/* ── Init ─────────────────────────────────────────────────────────────────── */

void plog_init(const char *ptolemy_dir, int quiet)
{
    if (g_init_done) return;
    g_init_done = 1;
    g_quiet     = quiet;

    if (!ptolemy_dir || !ptolemy_dir[0]) return;

    /* Build logs/ sub-directory path */
    snprintf(g_log_dir, sizeof(g_log_dir), "%s/logs", ptolemy_dir);
    mkdir(g_log_dir, 0700);

    /* Compute current 4-hour slot */
    time_t     now = time(NULL);
    struct tm *lt  = localtime(&now);
    int slot_hour  = (lt->tm_hour / 4) * 4;

    char logpath[4096];
    snprintf(logpath, sizeof(logpath),
             "%s/ptolemy_%04d%02d%02d_%02d%02d.log",
             g_log_dir,
             lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
             slot_hour, 0);

    g_log_fp = fopen(logpath, "a");

    /* Sunday (tm_wday == 0) at 10:00 — run GC */
    if (lt->tm_wday == 0 && lt->tm_hour == 10)
        run_gc();
}

/* ── Log ──────────────────────────────────────────────────────────────────── */

void plog(int level, const char *fmt, ...)
{
    /* Timestamp */
    time_t     now = time(NULL);
    struct tm *lt  = localtime(&now);
    char ts[32];
    snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d",
             lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
             lt->tm_hour, lt->tm_min, lt->tm_sec);

    va_list ap;

    /* Write to log file */
    if (g_log_fp) {
        fprintf(g_log_fp, "[%s] %s  ", ts, level_str(level));
        va_start(ap, fmt);
        vfprintf(g_log_fp, fmt, ap);
        va_end(ap);
        fputc('\n', g_log_fp);
        fflush(g_log_fp);
    }

    /* Mirror to stderr unless quiet (info) or always (warn/error) */
    if (!g_quiet || level >= PLOG_WARN) {
        FILE *err = stderr;
        fprintf(err, "[ptolemy] %s  ", level_str(level));
        va_start(ap, fmt);
        vfprintf(err, fmt, ap);
        va_end(ap);
        fputc('\n', err);
    }
}

/* ── Close ────────────────────────────────────────────────────────────────── */

void plog_close(void)
{
    if (g_log_fp) { fclose(g_log_fp); g_log_fp = NULL; }
}
