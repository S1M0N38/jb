/* meta.c — phase 6 metadata verbs: status, log, show, ps, wait, path, config.
   Metadata-only: reads .jb/sessions/<uuid>/metadata.json, no API calls. */
#include "meta.h"
#include "config.h"
#include "session.h"

#include <dirent.h>
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

/* mkdirs — mkdir -p for a single path (all intermediate components) */
int mkdirs(const char *path)
{
    char tmp[4096];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
    return 0;
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

/* ---- session scanner: reads every session's metadata.json ---- */

typedef struct {
    char uuid[JB_UUID_LEN];
    char status[16];
    char subject[256];
    char author[JB_UUID_LEN];
    char parent[JB_UUID_LEN];
    long long started_ms;
    long long ended_ms;
    long exit_code;
    int has_ended;
} session_rec;

typedef struct {
    session_rec *items;
    int n;
    int cap;
} session_list;

/* utc_to_epoch — days-from-civil (Hinnant) over UTC fields; no timegm. */
static long long utc_to_epoch(int y, int mo, int d, int h, int mi, int s)
{
    y -= mo <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long days = (long long)era * 146097 + (long long)doe - 719468;
    return days * 86400 + h * 3600 + mi * 60 + s;
}

/* iso_ms_to_epoch — parse "YYYY-MM-DDTHH:MM:SS.mmmZ" (the metadata
   timestamps) into epoch seconds; -1 on any mismatch. */
static long long iso_ms_to_epoch(const char *s)
{
    int y, mo, d, h, mi, sec, ms;
    if (sscanf(s, "%d-%d-%dT%d:%d:%d.%dZ", &y, &mo, &d, &h, &mi, &sec, &ms) != 7)
        return -1;
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31) return -1;
    if (h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 60) return -1;
    return utc_to_epoch(y, mo, d, h, mi, sec);
}

/* age_since — seconds ago as "3d/2h/12m/5s" (log/status style). */
static void age_short(long long now, long long then, char *out, size_t outlen)
{
    long long age = (now - then) / 1000;   /* ms → seconds */
    if (age < 60) snprintf(out, outlen, "%llds", age);
    else if (age < 3600) snprintf(out, outlen, "%lldm", age / 60);
    else if (age < 86400) snprintf(out, outlen, "%lldh", age / 3600);
    else snprintf(out, outlen, "%lldd", age / 86400);
}

/* age_mmss — seconds ago as "0:02" (ps style; h:mm:ss beyond an hour). */
static void age_mmss(long long now, long long then, char *out, size_t outlen)
{
    long long age = (now - then) / 1000;   /* ms → seconds */
    if (age < 0) age = 0;
    if (age >= 3600)
        snprintf(out, outlen, "%lld:%02lld:%02lld", age / 3600,
                 (age % 3600) / 60, age % 60);
    else
        snprintf(out, outlen, "%lld:%02lld", age / 60, age % 60);
}

static void session_list_add(session_list *list, const session_rec *rec)
{
    if (list->n == list->cap) {
        list->cap = list->cap ? list->cap * 2 : 16;
        list->items = realloc(list->items, (size_t)list->cap * sizeof(*list->items));
    }
    list->items[list->n++] = *rec;
}

static void session_list_free(session_list *list)
{
    free(list->items);
    list->items = NULL;
    list->n = list->cap = 0;
}

/* session_list_scan — load every session's metadata from
   <repo>/.jb/sessions/. Unparseable dirs are skipped (a session dir
   without metadata is not a session). Returns the number loaded. */
static int session_list_scan(const char *repo_root, session_list *list)
{
    char sessions_dir[4096];
    snprintf(sessions_dir, sizeof(sessions_dir), "%s/.jb/sessions", repo_root);
    DIR *d = opendir(sessions_dir);
    if (!d) return 0;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char meta[4096];
        snprintf(meta, sizeof(meta), "%s/%s/metadata.json", sessions_dir, de->d_name);
        char *json = read_file(meta);
        if (!json) continue;
        cJSON *root = cJSON_Parse(json);
        free(json);
        if (!root) { cJSON_Delete(root); continue; }

        session_rec rec;
        memset(&rec, 0, sizeof(rec));
        cJSON *v;
        v = cJSON_GetObjectItemCaseSensitive(root, "uuid");
        if (cJSON_IsString(v) && v->valuestring)
            strncpy(rec.uuid, v->valuestring, sizeof(rec.uuid) - 1);
        v = cJSON_GetObjectItemCaseSensitive(root, "status");
        if (cJSON_IsString(v) && v->valuestring)
            strncpy(rec.status, v->valuestring, sizeof(rec.status) - 1);
        v = cJSON_GetObjectItemCaseSensitive(root, "subject");
        if (cJSON_IsString(v) && v->valuestring)
            strncpy(rec.subject, v->valuestring, sizeof(rec.subject) - 1);
        v = cJSON_GetObjectItemCaseSensitive(root, "author");
        if (cJSON_IsString(v) && v->valuestring)
            strncpy(rec.author, v->valuestring, sizeof(rec.author) - 1);
        v = cJSON_GetObjectItemCaseSensitive(root, "parent");
        if (cJSON_IsString(v) && v->valuestring)
            strncpy(rec.parent, v->valuestring, sizeof(rec.parent) - 1);
        v = cJSON_GetObjectItemCaseSensitive(root, "started_at");
        if (cJSON_IsString(v) && v->valuestring)
            rec.started_ms = iso_ms_to_epoch(v->valuestring) * 1000;
        v = cJSON_GetObjectItemCaseSensitive(root, "ended_at");
        if (cJSON_IsString(v) && v->valuestring) {
            rec.ended_ms = iso_ms_to_epoch(v->valuestring) * 1000;
            rec.has_ended = 1;
        }
        v = cJSON_GetObjectItemCaseSensitive(root, "exit_code");
        if (cJSON_IsNumber(v))
            rec.exit_code = (long)v->valuedouble;
        cJSON_Delete(root);

        if (!rec.uuid[0]) continue;
        if (!rec.status[0]) continue;
        session_list_add(list, &rec);
    }
    closedir(d);
    return list->n;
}

/* rec_age_ms — the age anchor: started_at while working, ended_at once
   terminal (completed/committed/error). Callers compute now - anchor. */
static long long rec_age_anchor_ms(const session_rec *rec)
{
    if (strcmp(rec->status, "working") == 0 || !rec->has_ended)
        return rec->started_ms;
    return rec->ended_ms;
}

/* children_of — split the sessions authored by `author` into pending and
   committed lists (the ps/status children view). */
static void children_of(const session_list *list, const char *author,
                        session_list *pending, session_list *committed)
{
    for (int i = 0; i < list->n; i++) {
        if (strcmp(list->items[i].author, author) != 0) continue;
        if (strcmp(list->items[i].status, "committed") == 0)
            session_list_add(committed, &list->items[i]);
        else
            session_list_add(pending, &list->items[i]);
    }
}

static int cmp_started_desc(const void *a, const void *b)
{
    const session_rec *ra = a, *rb = b;
    if (ra->started_ms != rb->started_ms)
        return ra->started_ms < rb->started_ms ? 1 : -1;
    return strcmp(ra->uuid, rb->uuid);
}

/* short id — first 8 hex chars of a uuid */
static void id8(const char *uuid, char *out, size_t outlen)
{
    snprintf(out, outlen, "%.8s", uuid);
}

/* subject_trunc — subject ≤ 40 chars, ellipsized when longer. */
static void subject_trunc(const char *s, char *out, size_t outlen)
{
    if (strlen(s) <= 40) {
        snprintf(out, outlen, "%s", s);
        return;
    }
    snprintf(out, outlen, "%.37s...", s);
}

/* cmd_status — the repo glance (§7): session line (or working: N list),
   children (ps format), awaiting commit, and the always-on repo summary.
   Stale $JB_SESSION → stderr warning, no-session view, exit 0. */
int cmd_status(void)
{
    char repo_root[4096];
    if (require_repo(repo_root, sizeof(repo_root)) != 0) return 1;

    session_list list = {0};
    session_list_scan(repo_root, &list);

    /* repo summary counts */
    int n_committed = 0, n_working = 0, n_completed = 0, n_error = 0;
    for (int i = 0; i < list.n; i++) {
        const char *st = list.items[i].status;
        if (strcmp(st, "committed") == 0) n_committed++;
        else if (strcmp(st, "working") == 0) n_working++;
        else if (strcmp(st, "completed") == 0) n_completed++;
        else if (strcmp(st, "error") == 0) n_error++;
    }

    /* resolve @; stale env falls back to the no-session view (exit 0) */
    const char *env = getenv("JB_SESSION");
    char self[JB_UUID_LEN] = "";
    session_rec *self_rec = NULL;
    if (env && env[0]) {
        char err[512];
        int rc = session_resolve(repo_root, env, self, sizeof(self), err, sizeof(err));
        if (rc != 0) {
            fprintf(stderr, "jb: warning: JB_SESSION %s not found — ignoring\n", env);
        } else {
            for (int i = 0; i < list.n; i++) {
                if (strcmp(list.items[i].uuid, self) == 0) { self_rec = &list.items[i]; break; }
            }
        }
    }

    long long now = jb_epoch_ms();

    if (self_rec) {
        /* session line: identity, status, subject, age */
        char sid[9], age[64];
        id8(self_rec->uuid, sid, sizeof(sid));
        if (strcmp(self_rec->status, "working") == 0) {
            char a[32];
            age_short(now, rec_age_anchor_ms(self_rec), a, sizeof(a));
            snprintf(age, sizeof(age), "· %s", a);
        } else {
            char a[32];
            age_short(now, rec_age_anchor_ms(self_rec), a, sizeof(a));
            snprintf(age, sizeof(age), "· ended %s ago", a);
        }
        printf("session %s  (%s)  \"%s\" %s\n", sid, self_rec->status,
               self_rec->subject, age);

        /* children: pending (ps format) then committed count */
        session_list pending = {0}, committed = {0};
        children_of(&list, self, &pending, &committed);
        if (pending.n + committed.n > 0) {
            qsort(pending.items, (size_t)pending.n, sizeof(*pending.items), cmp_started_desc);
            printf("children: %d\n", pending.n);
            for (int i = 0; i < pending.n; i++) {
                char cid[9], age2[32];
                id8(pending.items[i].uuid, cid, sizeof(cid));
                age_mmss(now, rec_age_anchor_ms(&pending.items[i]), age2, sizeof(age2));
                printf("  %s  %s  %s  \"%s\"\n", cid, pending.items[i].status,
                       age2, pending.items[i].subject);
            }
            if (committed.n > 0)
                printf("committed: %d\n", committed.n);
        }
        session_list_free(&pending);
        session_list_free(&committed);
    } else {
        /* no-session view: working list */
        session_list working = {0};
        for (int i = 0; i < list.n; i++) {
            if (strcmp(list.items[i].status, "working") == 0)
                session_list_add(&working, &list.items[i]);
        }
        qsort(working.items, (size_t)working.n, sizeof(*working.items), cmp_started_desc);
        if (working.n > 0) {
            printf("working: %d (", working.n);
            for (int i = 0; i < working.n; i++) {
                char wid[9], subj[64];
                id8(working.items[i].uuid, wid, sizeof(wid));
                subject_trunc(working.items[i].subject, subj, sizeof(subj));
                printf("%s%s \"%s\"", i ? ", " : "", wid, subj);
            }
            printf(")\n");
        }
        session_list_free(&working);
    }

    /* awaiting commit: completed/error not yet committed, newest first */
    session_list awaiting = {0};
    for (int i = 0; i < list.n; i++) {
        const char *st = list.items[i].status;
        if (strcmp(st, "completed") == 0 || strcmp(st, "error") == 0)
            session_list_add(&awaiting, &list.items[i]);
    }
    qsort(awaiting.items, (size_t)awaiting.n, sizeof(*awaiting.items), cmp_started_desc);
    if (awaiting.n > 0) {
        printf("awaiting commit: %d (", awaiting.n);
        for (int i = 0; i < awaiting.n; i++) {
            char aid[9];
            id8(awaiting.items[i].uuid, aid, sizeof(aid));
            printf("%s%s", i ? ", " : "", aid);
        }
        printf(")\n");
    }
    session_list_free(&awaiting);

    printf("repo: %d sessions · %d committed · %d working · %d completed · %d error\n",
           list.n, n_committed, n_working, n_completed, n_error);
    session_list_free(&list);
    return 0;
}

/* ---- jb log: flat + the committed forest ---- */

/* find_session — index of the session with the given uuid in list, or -1 */
static int find_session(const session_list *list, const char *uuid)
{
    for (int i = 0; i < list->n; i++)
        if (strcmp(list->items[i].uuid, uuid) == 0) return i;
    return -1;
}

/* cmd_log — flat: id<TAB>status<TAB>author<TAB>age<TAB>subject, newest
   first. --graph: the committed forest only — roots oldest-first,
   children by start time; <id> [error] <kind> "<subject>" with kind
   root|fork|fresh; every edge is ├── / └── (§7). */
int cmd_log(const char *graph_arg)
{
    char repo_root[4096];
    if (require_repo(repo_root, sizeof(repo_root)) != 0) return 1;

    int graph = 0;
    if (graph_arg) {
        if (strcmp(graph_arg, "--graph") == 0) graph = 1;
        else {
            fprintf(stderr, "jb: unknown option '%s' for 'log'\n", graph_arg);
            return 2;
        }
    }

    session_list list = {0};
    session_list_scan(repo_root, &list);
    long long now = jb_epoch_ms();

    if (!graph) {
        qsort(list.items, (size_t)list.n, sizeof(*list.items), cmp_started_desc);
        for (int i = 0; i < list.n; i++) {
            session_rec *r = &list.items[i];
            char sid[9], author[9], age[32], subj[64];
            id8(r->uuid, sid, sizeof(sid));
            if (r->author[0]) id8(r->author, author, sizeof(author));
            else snprintf(author, sizeof(author), "-");
            age_short(now, rec_age_anchor_ms(r), age, sizeof(age));
            subject_trunc(r->subject, subj, sizeof(subj));
            printf("%s\t%s\t%s\t%s\t%s\n", sid, r->status, author, age, subj);
        }
        session_list_free(&list);
        return 0;
    }

    /* graph: committed nodes only, linked to their nearest committed
       ancestor (fork) or their committed author (fresh). kind: root
       (no parent, no author) / fork (continuation) / fresh (new
       context — spawned by a session, no --fork parent). */
    enum { KIND_ROOT, KIND_FORK, KIND_FRESH };
    typedef struct {
        session_rec *rec;
        int parent;   /* index into nodes, or -1 for top-level */
        int kind;
    } graph_node;

    int n = 0;
    for (int i = 0; i < list.n; i++)
        if (strcmp(list.items[i].status, "committed") == 0) n++;
    graph_node *nodes = calloc((size_t)(n ? n : 1), sizeof(*nodes));
    int ni = 0;
    for (int i = 0; i < list.n; i++) {
        if (strcmp(list.items[i].status, "committed") != 0) continue;
        nodes[ni].rec = &list.items[i];
        nodes[ni].parent = -1;
        nodes[ni].kind = KIND_ROOT;
        ni++;
    }

    for (int i = 0; i < ni; i++) {
        /* kind by origin (cli.html): fork = continuation (parent set),
           fresh = new context (no parent, spawned by a session), root =
           human run (no parent, no author). */
        nodes[i].kind = nodes[i].rec->parent[0]
                            ? KIND_FORK
                            : (nodes[i].rec->author[0] ? KIND_FRESH : KIND_ROOT);
        /* attach: a fork hangs under its nearest committed ancestor; a
           fresh hangs under its author's node (the spawner) when the
           author is itself committed. */
        int attach = -1;
        if (nodes[i].kind == KIND_FORK) {
            const char *p = nodes[i].rec->parent;
            int guard = 0;
            while (p[0] && guard++ < 64) {
                int idx = find_session(&list, p);
                if (idx < 0) break;             /* chain ends */
                if (strcmp(list.items[idx].status, "committed") == 0) {
                    attach = idx;
                    break;
                }
                p = list.items[idx].parent;     /* keep walking */
            }
        } else if (nodes[i].kind == KIND_FRESH) {
            attach = find_session(&list, nodes[i].rec->author);
            if (attach >= 0
                && strcmp(list.items[attach].status, "committed") != 0)
                attach = -1;
        }
        if (attach >= 0) {
            for (int j = 0; j < ni; j++) {
                if (nodes[j].rec == &list.items[attach]) {
                    nodes[i].parent = j;
                    break;
                }
            }
        }
    }

    /* top-level nodes: no committed ancestor, oldest first */
    int *roots = malloc((size_t)(ni ? ni : 1) * sizeof(*roots));
    int rn = 0;
    for (int i = 0; i < ni; i++)
        if (nodes[i].parent < 0) roots[rn++] = i;
    /* sort roots by started asc */
    for (int i = 0; i < rn; i++)
        for (int j = i + 1; j < rn; j++)
            if (nodes[roots[j]].rec->started_ms < nodes[roots[i]].rec->started_ms) {
                int t = roots[i]; roots[i] = roots[j]; roots[j] = t;
            }

    const char *kind_name[] = { "root", "fork", "fresh" };
    for (int r = 0; r < rn; r++) {
        int ri = roots[r];
        /* children of ri, by start time asc */
        int *kids = malloc((size_t)(ni ? ni : 1) * sizeof(*kids));
        int kn = 0;
        for (int i = 0; i < ni; i++)
            if (nodes[i].parent == ri) kids[kn++] = i;
        for (int i = 0; i < kn; i++)
            for (int j = i + 1; j < kn; j++)
                if (nodes[kids[j]].rec->started_ms < nodes[kids[i]].rec->started_ms) {
                    int t = kids[i]; kids[i] = kids[j]; kids[j] = t;
                }

        char sid[9], subj[64];
        id8(nodes[ri].rec->uuid, sid, sizeof(sid));
        subject_trunc(nodes[ri].rec->subject, subj, sizeof(subj));
        printf("%s  %s%s  \"%s\"\n", sid,
               nodes[ri].rec->exit_code != 0 ? "[error]  " : "",
               kind_name[nodes[ri].kind], subj);
        for (int k = 0; k < kn; k++) {
            int ci = kids[k];
            id8(nodes[ci].rec->uuid, sid, sizeof(sid));
            subject_trunc(nodes[ci].rec->subject, subj, sizeof(subj));
            printf("%s %s  %s%s  \"%s\"\n",
                   k == kn - 1 ? "└──" : "├──", sid,
                   nodes[ci].rec->exit_code != 0 ? "[error]  " : "",
                   kind_name[nodes[ci].kind], subj);
        }
        free(kids);
    }
    free(roots);
    free(nodes);
    session_list_free(&list);
    return 0;
}

/* cmd_wait — poll metadata.json every 250 ms until status ∈ {completed,
   error}; stdout silent, one waiting line on stderr; refuses the current
   session (self-deadlock). 0 completed · 1 error/not found · 2 usage (§7) */
int cmd_wait(const char *id_arg)
{
    char repo_root[4096];
    if (require_repo(repo_root, sizeof(repo_root)) != 0) return 1;

    if (!id_arg) {
        fprintf(stderr, "jb: wait requires a session ID (see 'jb help wait')\n");
        return 2;
    }
    char uuid[JB_UUID_LEN];
    if (jb_resolve_id_arg(repo_root, id_arg, uuid, sizeof(uuid)) != 0) return 1;

    /* refuse the current session: waiting on @ is a self-deadlock */
    const char *env = getenv("JB_SESSION");
    if (env && env[0]) {
        char cur[JB_UUID_LEN];
        char err[512];
        if (session_resolve(repo_root, env, cur, sizeof(cur), err, sizeof(err)) == 0
            && strcmp(cur, uuid) == 0) {
            fprintf(stderr, "jb: cannot wait on the current session\n");
            return 1;
        }
    }

    char meta[4096];
    snprintf(meta, sizeof(meta), "%s/.jb/sessions/%s/metadata.json",
             repo_root, uuid);
    fprintf(stderr, "jb: waiting for %.8s…\n", uuid);

    for (;;) {
        char *json = read_file(meta);
        if (!json) return 1;   /* vanished mid-wait → not found */
        cJSON *root = cJSON_Parse(json);
        free(json);
        if (!root) { cJSON_Delete(root); return 1; }
        cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "status");
        const char *status = cJSON_IsString(v) && v->valuestring
                                 ? v->valuestring : "";
        int rc = 0;
        if (strcmp(status, "completed") == 0) rc = 0;
        else if (strcmp(status, "error") == 0) rc = 1;
        else rc = -1;   /* keep polling (working / committed / unknown) */
        cJSON_Delete(root);
        if (rc >= 0) return rc;
        usleep(250000);
    }
}

/* cmd_ps — children of @: pending lines id<TAB>status<TAB>age<TAB>subject
   (newest first), then committed: N when N > 0 (§7) */
int cmd_ps(void)
{
    char repo_root[4096];
    if (require_repo(repo_root, sizeof(repo_root)) != 0) return 1;

    char self[JB_UUID_LEN];
    if (jb_resolve_id_arg(repo_root, "@", self, sizeof(self)) != 0) return 1;

    session_list list = {0};
    session_list_scan(repo_root, &list);

    session_list pending = {0}, committed = {0};
    children_of(&list, self, &pending, &committed);
    qsort(pending.items, (size_t)pending.n, sizeof(*pending.items), cmp_started_desc);

    long long now = jb_epoch_ms();
    for (int i = 0; i < pending.n; i++) {
        session_rec *r = &pending.items[i];
        char sid[9], age[32];
        id8(r->uuid, sid, sizeof(sid));
        age_mmss(now, rec_age_anchor_ms(r), age, sizeof(age));
        printf("%s\t%s\t%s\t\"%s\"\n", sid, r->status, age, r->subject);
    }
    if (committed.n > 0)
        printf("committed: %d\n", committed.n);

    session_list_free(&pending);
    session_list_free(&committed);
    session_list_free(&list);
    return 0;
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

/* ---- jb config: git-style global/local mirror (§7) ---- */

typedef struct {
    char key[128];
    char val[256];
} cfg_kv;

/* kv_set — copy key/value into kvs[n] (the cJSON tree is freed after
   collection, so nothing may point into it). */
static void kv_set(cfg_kv *kv, const char *key, const char *val)
{
    strncpy(kv->key, key, sizeof(kv->key) - 1);
    snprintf(kv->val, sizeof(kv->val), "%s", val ? val : "");
}

/* collect_config_keys — copy a file's keys into kvs (string values as-is,
   numbers formatted). Returns the number collected. */
static int collect_config_keys(cJSON *root, cfg_kv *kvs, int max)
{
    int n = 0;
    for (cJSON *c = root->child; c && n < max; c = c->next) {
        if (!c->string) continue;
        if (cJSON_IsString(c) && c->valuestring) {
            kv_set(&kvs[n], c->string, c->valuestring);
            n++;
        } else if (cJSON_IsNumber(c)) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", (long long)c->valuedouble);
            kv_set(&kvs[n], c->string, buf);
            n++;
        }
    }
    return n;
}

static int cmp_cfg_kv(const void *a, const void *b)
{
    return strcmp(((const cfg_kv *)a)->key, ((const cfg_kv *)b)->key);
}

/* cmd_config [--global|--local] [KEY [VALUE]] — list effective settings
   (local merged over global, sorted key=value), print a key, or set a key
   (local file by default, --global for the global file). Any key accepted;
   values stored as strings, coerced at use. 0 · 1 · 2 usage (§7). */
int cmd_config(int argc, char **argv)
{
    int global = 0;
    int i = 0;
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "--global") == 0) global = 1;
        else if (strcmp(argv[i], "--local") == 0) global = 0;
        else {
            fprintf(stderr, "jb: unknown option '%s' for 'config'\n", argv[i]);
            return 2;
        }
        i++;
    }
    if (argc - i > 2) {
        fprintf(stderr, "jb: too many arguments for 'config' (see 'jb help config')\n");
        return 2;
    }

    char gpath[4096];
    config_global_path(gpath, sizeof(gpath));
    char lpath[4096] = "";
    if (!global) {
        char repo_root[4096];
        if (require_repo(repo_root, sizeof(repo_root)) == 0)
            snprintf(lpath, sizeof(lpath), "%s/.jb/config.json", repo_root);
    }

    /* ---- set: KEY VALUE → local by default, --global for global ---- */
    if (argc - i == 2) {
        const char *path = global ? gpath : (lpath[0] ? lpath : NULL);
        if (!path) {
            fprintf(stderr, "jb: fatal: not a jb repository (run 'jb init')\n");
            return 1;
        }
        /* the global dir may not exist yet — create it (git config
           creates ~/.gitconfig's directory on demand too) */
        char dir[4096];
        snprintf(dir, sizeof(dir), "%s", path);
        char *slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            if (dir[0]) mkdirs(dir);
        }

        cJSON *root = NULL;
        char *json = read_file(path);
        if (json) {
            root = cJSON_Parse(json);
            free(json);
        }
        if (!root) root = cJSON_CreateObject();
        cJSON_DeleteItemFromObjectCaseSensitive(root, argv[i]);
        cJSON_AddStringToObject(root, argv[i], argv[i + 1]);
        char *out = cJSON_PrintUnformatted(root);
        char tmp[4096];
        snprintf(tmp, sizeof(tmp), "%s.tmp", path);
        FILE *f = fopen(tmp, "w");
        int rc = 1;
        if (f && out) {
            fputs(out, f);
            fclose(f);
            if (rename(tmp, path) == 0) rc = 0;
            else fprintf(stderr, "jb: %s: cannot write config\n", path);
        } else {
            if (f) fclose(f);
            fprintf(stderr, "jb: %s: cannot write config\n", path);
        }
        free(out);
        cJSON_Delete(root);
        return rc;
    }

    /* ---- get: KEY → the effective value (local over global) ---- */
    if (argc - i == 1) {
        const char *key = argv[i];
        const char *paths[2] = { lpath[0] ? lpath : NULL, gpath };
        for (int p = 0; p < 2; p++) {
            if (!paths[p]) continue;
            char *json = read_file(paths[p]);
            if (!json) continue;
            cJSON *root = cJSON_Parse(json);
            free(json);
            if (!root) { cJSON_Delete(root); continue; }
            cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
            if (cJSON_IsString(v) && v->valuestring) {
                printf("%s\n", v->valuestring);
                cJSON_Delete(root);
                return 0;
            }
            if (cJSON_IsNumber(v)) {
                printf("%lld\n", (long long)v->valuedouble);
                cJSON_Delete(root);
                return 0;
            }
            cJSON_Delete(root);
        }
        fprintf(stderr, "jb: no such key '%s'\n", key);
        return 1;
    }

    /* ---- list: local merged over global, sorted ---- */
    cfg_kv kvs[256];
    int n = 0;
    char *json = read_file(gpath);
    if (json) {
        cJSON *root = cJSON_Parse(json);
        free(json);
        if (root) {
            n = collect_config_keys(root, kvs, 256);
            cJSON_Delete(root);
        }
    }
    json = read_file(lpath);
    if (json) {
        cJSON *root = cJSON_Parse(json);
        free(json);
        if (root) {
            for (cJSON *c = root->child; c && n < 256; c = c->next) {
                if (!c->string) continue;
                int found = 0;
                for (int k = 0; k < n; k++) {
                    if (strcmp(kvs[k].key, c->string) == 0) {
                        if (cJSON_IsString(c) && c->valuestring)
                            kv_set(&kvs[k], c->string, c->valuestring);
                        else if (cJSON_IsNumber(c)) {
                            char buf[32];
                            snprintf(buf, sizeof(buf), "%lld",
                                     (long long)c->valuedouble);
                            kv_set(&kvs[k], c->string, buf);
                        }
                        found = 1;
                        break;
                    }
                }
                if (!found && (cJSON_IsString(c) || cJSON_IsNumber(c))) {
                    if (cJSON_IsString(c))
                        kv_set(&kvs[n], c->string, c->valuestring);
                    else {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%lld",
                                 (long long)c->valuedouble);
                        kv_set(&kvs[n], c->string, buf);
                    }
                    n++;
                }
            }
            cJSON_Delete(root);
        }
    }
    qsort(kvs, (size_t)n, sizeof(*kvs), cmp_cfg_kv);
    for (int k = 0; k < n; k++)
        printf("%s=%s\n", kvs[k].key, kvs[k].val);
    return 0;
}
