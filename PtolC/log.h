/*
 * PtolC/log.h — Ptolemy logging API.
 *
 * Log files live in ~/.ptolemy/logs/ and rotate every 4 hours:
 *   ptolemy_YYYYMMDD_HHMM.log  (HHMM = 0000, 0400, 0800, 1200, 1600, 2000)
 *
 * GC runs on Sunday at 10:00 and removes log files where both atime and
 * mtime are older than PLOG_GC_MAX_AGE_DAYS.
 */

#ifndef LOG_H
#define LOG_H

/* Log levels — passed to plog(). */
#define PLOG_INFO   0
#define PLOG_WARN   1
#define PLOG_ERROR  2

/* Days after which an untouched log file is eligible for GC. */
#define PLOG_GC_MAX_AGE_DAYS  30

/**
 * Initialise logging.  Creates ~/.ptolemy/logs/ if absent, opens the
 * current 4-hour slot file, and runs the Sunday 10:00 GC if applicable.
 * Safe to call multiple times — only the first call has effect.
 *
 * :param ptolemy_dir: Path to the ~/.ptolemy directory (g_ptolemy_dir).
 * :param quiet:       If non-zero, suppress stderr output (log to file only).
 */
void plog_init(const char *ptolemy_dir, int quiet);

/**
 * Write a timestamped log entry.
 *
 * :param level: PLOG_INFO, PLOG_WARN, or PLOG_ERROR.
 * :param fmt:   printf-style format string.
 */
void plog(int level, const char *fmt, ...);

/**
 * Close the current log file handle (call before exit if desired).
 * plog() after plog_close() silently becomes a no-op.
 */
void plog_close(void);

#endif /* LOG_H */
