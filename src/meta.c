/* meta.c — phase 6 metadata verbs: status, log, show, ps, wait, path, config.
   Metadata-only: reads .jb/sessions/<uuid>/metadata.json, no API calls. */
#include "meta.h"
#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int path_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int path_is_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* jb_find_repo — walk up from start looking for .jb/. Returns 0 with the
   repo root in out, or -1 when no repo encloses start. */
int jb_find_repo(const char *start, char *out, size_t outlen)
{
    char dir[4096];
    strncpy(dir, start, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';

    for (;;) {
        char jbdir[4096];
        snprintf(jbdir, sizeof(jbdir), "%s/.jb", dir);
        if (path_is_dir(jbdir)) {
            snprintf(out, outlen, "%s", dir);
            return 0;
        }
        char *slash = strrchr(dir, '/');
        if (!slash || slash == dir) return -1;  /* reached the root */
        *slash = '\0';
    }
}

/* Resolve a session ID argument for --fork/--seed/@: "@" reads $JB_SESSION
   (unset → "jb: JB_SESSION not set"). Prints the reference error and
   returns -1 on failure, else 0 with the full uuid in out. */
int jb_resolve_id_arg(const char *repo_root, const char *arg,
                      char *out, size_t outlen)
{
    char err[512];
    const char *id = arg;
    int from_env = 0;
    if (strcmp(arg, "@") == 0) {
        const char *env = getenv("JB_SESSION");
        if (!env || !env[0]) {
            fprintf(stderr, "jb: JB_SESSION not set\n");
            return -1;
        }
        id = env;
        from_env = 1;
    }
    int rc = session_resolve(repo_root, id, out, outlen, err, sizeof(err));
    if (rc == 0) return 0;
    if (from_env && rc == 1)
        fprintf(stderr, "jb: JB_SESSION %s not found\n", id);
    else
        fprintf(stderr, "jb: %s\n", err);
    return -1;
}

/* require_repo — resolve the enclosing repo or print the fatal and return
   -1. Every metadata verb is repo-gated (exit 1 outside a repo). */
static int require_repo(char *repo_root, size_t len)
{
    char cwd_buf[4096];
    const char *cwd = getcwd(cwd_buf, sizeof(cwd_buf)) ? cwd_buf : ".";
    if (jb_find_repo(cwd, repo_root, len) != 0) {
        fprintf(stderr, "jb: fatal: not a jb repository (run 'jb init')\n");
        return -1;
    }
    return 0;
}

/* cmd_path — print the absolute session directory (§7) */
int cmd_path(const char *id_arg)
{
    char repo_root[4096];
    if (require_repo(repo_root, sizeof(repo_root)) != 0) return 1;

    if (!id_arg) {
        fprintf(stderr, "jb: path requires a session ID (see 'jb help path')\n");
        return 2;
    }
    char uuid[JB_UUID_LEN];
    if (jb_resolve_id_arg(repo_root, id_arg, uuid, sizeof(uuid)) != 0)
        return 1;

    printf("%s/.jb/sessions/%s\n", repo_root, uuid);
    return 0;
}
