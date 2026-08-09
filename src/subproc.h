/* subproc.h — pid-capturing popen replacement.
   popen() does not expose the child pid, so a signal handler cannot kill
   the child to unblock a read. subproc_open runs CMD via /bin/sh -c with
   stdout on a pipe (popen semantics) and returns the child pid — the
   SIGINT/SIGTERM handler kills it (async-signal-safe), which closes the
   pipe and wakes the blocking read with EOF. */
#ifndef JB_SUBPROC_H
#define JB_SUBPROC_H

#include <stdio.h>
#include <sys/types.h>

/* Run CMD via /bin/sh -c with stdout on a pipe. On success returns the
   stream (NULL on failure) and stores the child pid in *pid_out. */
FILE *subproc_open(const char *cmd, pid_t *pid_out);

/* Close the stream and reap the child. Returns the wait status (pclose
   semantics — check with WIFEXITED/WEXITSTATUS), or -1 on error. */
int subproc_close(FILE *fp, pid_t pid);

#endif
