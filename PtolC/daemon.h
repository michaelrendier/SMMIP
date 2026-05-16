/*
 * PtolC/daemon.h — Ptolemy daemon mode API.
 *
 * The daemon loads the checkpoint once and holds it in memory.
 * Clients connect via a Unix domain socket and send line-based commands.
 *
 * Protocol (line-based, UTF-8):
 *   Client → Server:
 *     HEAR <query text>\n    — hear/speak, returns response
 *     STATUS\n               — field status
 *     HEALTH\n               — field health report
 *     QUIT\n                 — close this connection
 *
 *   Server → Client:
 *     <response lines>
 *     .\n                    — end-of-message sentinel
 *
 * Socket path search order:
 *   1. -S <path> flag
 *   2. $PTOLEMY_SOCKET env var
 *   3. ~/.ptolemy/ptolemy.sock
 *
 * Systemd socket activation:
 *   If $LISTEN_FDS is set the daemon uses fd 3 (SD_LISTEN_FDS_START)
 *   directly instead of creating and binding its own socket.
 *   No dependency on libsystemd is required.
 */

#ifndef DAEMON_H
#define DAEMON_H

#include "monad.h"

/**
 * Start the daemon: bind the Unix socket, write a PID file, and enter
 * the serve loop.  Blocks until SIGTERM or SIGINT is received, then
 * saves the checkpoint and exits.
 *
 * :param m:         Loaded Monad to serve.
 * :param sock_path: Unix socket path.  NULL → ~/.ptolemy/ptolemy.sock.
 * :param ckpt_path: Checkpoint to save on shutdown.  NULL → no save.
 * :param pid_path:  PID file path.  NULL → ~/.ptolemy/ptolemy.pid.
 * :param verbose:   Verbosity passed to monad_speak().
 * :returns:         0 on clean shutdown, -1 on fatal error.
 */
int daemon_serve(Monad *m, const char *sock_path, const char *ckpt_path,
                 const char *pid_path, int verbose);

/**
 * Send a HEAR query to a running daemon and print the response to stdout.
 *
 * :param query:     Query string.
 * :param sock_path: Unix socket path.  NULL → ~/.ptolemy/ptolemy.sock.
 * :returns:         0 on success, -1 if connection failed.
 */
int daemon_query(const char *query, const char *sock_path);

/**
 * Send a STATUS or HEALTH command to a running daemon and print to stdout.
 *
 * :param cmd:       "STATUS" or "HEALTH".
 * :param sock_path: Unix socket path.
 * :returns:         0 on success, -1 if connection failed.
 */
int daemon_command(const char *cmd, const char *sock_path);

/**
 * Resolve the effective socket path: flag → $PTOLEMY_SOCKET → default.
 * Returns a pointer to a static buffer.
 *
 * :param flag_path:    Value of -S flag, or NULL.
 * :param ptolemy_dir:  g_ptolemy_dir from main.
 */
const char *daemon_sock_path(const char *flag_path, const char *ptolemy_dir);

#endif /* DAEMON_H */
