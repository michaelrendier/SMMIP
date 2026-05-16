/*
 * PtolC/ingest.h — Filesystem ingest API for the C Monad.
 *
 * Only regular files whose extension is on the Native Space whitelist are
 * ingested.  The whitelist enforces Native Space addressability: tokens must
 * carry semantic content that can be assigned a stratum address in the Dixon
 * tower.  Config tokens, registry keys, and binary-derived strings are flat
 * Cartesian noise and are excluded by design.
 *
 * Usage:
 *   ingest_path(m, "/home/user/Documents", verbose, checkpoint_path);
 */

#ifndef INGEST_H
#define INGEST_H

#include "monad.h"

/**
 * Walk ``root`` recursively and learn every whitelisted text file into ``m``.
 *
 * :param m:            Target Monad.
 * :param root:         Directory (or single file) to ingest.
 * :param verbose:      Verbosity level passed to monad_learn().
 * :param ckpt_path:    If non-NULL, save checkpoint here after every
 *                      INGEST_SAVE_EVERY files.  Pass NULL to skip
 *                      mid-run saves (checkpoint still saved by caller).
 * :returns:            Number of files successfully ingested, or -1 on
 *                      fatal error (e.g. root does not exist).
 */
int ingest_path(Monad *m, const char *root, int verbose,
                const char *ckpt_path);

/* Files between periodic checkpoint saves during a long ingest run. */
#define INGEST_SAVE_EVERY  500

#endif /* INGEST_H */
