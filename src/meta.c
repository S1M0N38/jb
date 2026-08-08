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

/* ---- shared metadata reading ---- */

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t nread = fread(buf, 1, (size_t)len, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

/* session_dir_path — resolve ID (or @ when id_arg is NULL) and write the
   session directory into out. Returns 0, or -1 with the error printed. */
static int session_dir_path(const char *repo_root, const char *id_arg,
                            char *out, size_t outlen)
{
    char uuid[JB_UUID_LEN];
    if (jb_resolve_id_arg(repo_root, id_arg ? id_arg : "@",
                          uuid, sizeof(uuid)) != 0)
        return -1;
    snprintf(out, outlen, "%s/.jb/sessions/%s", repo_root, uuid);
    return 0;
}

/* print_pretty_2 — cJSON formatted output uses tab indents; the reference
   (§7 show) specifies indent 2. Tabs appear only as indentation (string
   tabs are escaped), so each leading tab becomes two spaces. */
static void print_pretty_2(cJSON *root)
{
    char *json = cJSON_PrintBuffered(root, 0, 1);
    if (!json) return;
    for (char *line = json; *line;) {
        char *nl = strchr(line, '\n');
        int tab = 0;
        while (line[tab] == '\t') tab++;
        for (int i = 0; i < tab * 2; i++) putchar(' ');
        fwrite(line + tab, 1, (nl ? (size_t)(nl - line) : strlen(line)) - (size_t)tab, stdout);
        if (nl) { putchar('\n'); line = nl + 1; } else break;
    }
    putchar('\n');
    free(json);
}

/* cmd_show — pretty-print metadata.json (indent 2); the metadata path on
   stderr; ID defaults to @ (§7) */
int cmd_show(const char *id_arg)
{
    char repo_root[4096];
    if (require_repo(repo_root, sizeof(repo_root)) != 0) return 1;

    char dir[4096];
    if (session_dir_path(repo_root, id_arg, dir, sizeof(dir)) != 0) return 1;

    char meta_path[4096];
    snprintf(meta_path, sizeof(meta_path), "%s/metadata.json", dir);
    char *json = read_file(meta_path);
    if (!json) {
        fprintf(stderr, "jb: no metadata for '%s'\n", id_arg ? id_arg : "@");
        return 1;
    }
    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) {
        fprintf(stderr, "jb: %s: cannot parse metadata\n", meta_path);
        return 1;
    }
    fprintf(stderr, "jb: metadata: %s\n", meta_path);
    print_pretty_2(root);
    cJSON_Delete(root);
    return 0;
}
