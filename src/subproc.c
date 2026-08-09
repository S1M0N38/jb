/* subproc.c — pid-capturing popen replacement (see subproc.h) */
#include "subproc.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

FILE *subproc_open(const char *cmd, pid_t *pid_out)
{
    int p[2];
    if (pipe(p) != 0) return NULL;

    pid_t pid = fork();
    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        return NULL;
    }

    if (pid == 0) {
        /* Child: stdout → pipe write end */
        close(p[0]);
        dup2(p[1], STDOUT_FILENO);
        close(p[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    /* Parent: read end */
    close(p[1]);
    FILE *fp = fdopen(p[0], "r");
    if (!fp) {
        close(p[0]);
        /* Reap the child — the stream is gone, nothing else will */
        int status;
        waitpid(pid, &status, 0);
        return NULL;
    }
    *pid_out = pid;
    return fp;
}

int subproc_close(FILE *fp, pid_t pid)
{
    int rc = fclose(fp);
    /* Reap regardless of the fclose result — a skipped waitpid leaks a
       zombie. */
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return rc == 0 ? status : -1;
}
