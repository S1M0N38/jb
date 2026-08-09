/* session.c — session storage: .jb/sessions/<uuid>/{metadata,session,events}.json */
#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

static int generate_uuid(char *out, size_t outlen)
{
    /* UUID v4 from /dev/urandom */
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return -1;

    unsigned char bytes[16];
    if (fread(bytes, 1, 16, f) != 16) {
        fclose(f);
        return -1;
    }
    fclose(f);

    /* Set version (4) and variant bits */
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    snprintf(out, outlen,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);

    return 0;
}

static int mkdirs(const char *path)
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

int session_init(jb_session *sess, const char *repo_root)
{
    memset(sess, 0, sizeof(*sess));

    if (generate_uuid(sess->uuid, sizeof(sess->uuid)) != 0)
        return -1;

    snprintf(sess->session_dir, sizeof(sess->session_dir),
        "%s/.jb/sessions/%s", repo_root, sess->uuid);

    snprintf(sess->session_path, sizeof(sess->session_path),
        "%s/session.jsonl", sess->session_dir);

    snprintf(sess->events_path, sizeof(sess->events_path),
        "%s/events.jsonl", sess->session_dir);

    snprintf(sess->metadata_path, sizeof(sess->metadata_path),
        "%s/metadata.json", sess->session_dir);

    /* Create session directory */
    if (mkdirs(sess->session_dir) != 0)
        return -1;

    /* Open the conversation file for appending */
    sess->session_fp = fopen(sess->session_path, "a");
    if (!sess->session_fp) return -1;

    /* Open the events stream for appending */
    sess->events_fp = fopen(sess->events_path, "a");
    if (!sess->events_fp) {
        fclose(sess->session_fp);
        sess->session_fp = NULL;
        return -1;
    }

    return 0;
}

int session_append_pi(jb_session *sess, const char *json_line)
{
    if (!sess->session_fp) return -1;
    fprintf(sess->session_fp, "%s\n", json_line);
    fflush(sess->session_fp);
    return 0;
}

/* ---- Entry base (§3.2): id, parentId chain, ISO-ms timestamp ---- */

static int id8_in_use(const jb_session *sess, const char *id8)
{
    for (int i = 0; i < sess->used_n; i++) {
        if (strcmp(sess->used_ids[i], id8) == 0) return 1;
    }
    return 0;
}

static void id8_add_used(jb_session *sess, const char *id8)
{
    if (sess->used_n >= sess->used_cap) {
        sess->used_cap = sess->used_cap ? sess->used_cap * 2 : 16;
        sess->used_ids = realloc(sess->used_ids, (size_t)sess->used_cap * sizeof(char *));
    }
    sess->used_ids[sess->used_n] = strdup(id8);
    sess->used_n++;
}

int session_append_message(jb_session *sess, cJSON *message)
{
    char id8[16], ts[40];
    do {
        jb_id8(id8, sizeof(id8));
    } while (id8_in_use(sess, id8));
    id8_add_used(sess, id8);
    jb_iso8601_ms(ts, sizeof(ts));

    cJSON *entry = cJSON_CreateObject();
    cJSON_AddStringToObject(entry, "type", "message");
    cJSON_AddStringToObject(entry, "id", id8);
    if (sess->last_entry_id[0])
        cJSON_AddStringToObject(entry, "parentId", sess->last_entry_id);
    else
        cJSON_AddNullToObject(entry, "parentId");
    cJSON_AddStringToObject(entry, "timestamp", ts);
    cJSON_AddItemReferenceToObject(entry, "message", message);

    char *s = cJSON_PrintUnformatted(entry);
    cJSON_Delete(entry);  /* reference — the message itself is NOT freed */
    if (!s) return -1;

    int rc = session_append_pi(sess, s);
    if (rc == 0)
        snprintf(sess->last_entry_id, sizeof(sess->last_entry_id), "%s", id8);
    free(s);
    return rc;
}

int session_append_event(jb_session *sess, const char *json_line)
{
    if (!sess->events_fp) return -1;
    fprintf(sess->events_fp, "%s\n", json_line);
    fflush(sess->events_fp);
    return 0;
}

void session_set_author(jb_session *sess, const char *author)
{
    if (author && author[0]) {
        strncpy(sess->author, author, JB_UUID_LEN - 1);
        sess->author[JB_UUID_LEN - 1] = '\0';
    }
}

void session_set_parent(jb_session *sess, const char *uuid, const char *session_path)
{
    if (uuid && uuid[0]) {
        strncpy(sess->parent, uuid, JB_UUID_LEN - 1);
        sess->parent[JB_UUID_LEN - 1] = '\0';
    }
    if (session_path && session_path[0]) {
        strncpy(sess->parent_path, session_path, sizeof(sess->parent_path) - 1);
        sess->parent_path[sizeof(sess->parent_path) - 1] = '\0';
    }
}

/* ---- ID resolution (reference §7: full uuid | unique 4+ hex prefix) ---- */

int session_resolve(const char *repo_root, const char *id,
                    char *out, size_t outlen, char *err, size_t errlen)
{
    char sessions_dir[4096];
    snprintf(sessions_dir, sizeof(sessions_dir), "%s/.jb/sessions", repo_root);

    /* Candidate session dir names. Capped — more matches is still
       ambiguous; the message just lists the first few. */
    char *matches[12];
    int n = 0;
    size_t idlen = strlen(id);

    DIR *d = opendir(sessions_dir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL && n < 12) {
            if (de->d_name[0] == '.') continue;
            /* Only directories count as sessions */
            char full[4224];
            snprintf(full, sizeof(full), "%s/%s", sessions_dir, de->d_name);
            struct stat st;
            if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            if (strcmp(de->d_name, id) == 0) {
                /* exact full-uuid match wins outright */
                snprintf(out, outlen, "%s", de->d_name);
                closedir(d);
                return 0;
            }
            if (idlen >= 4 && strncmp(de->d_name, id, idlen) == 0)
                matches[n++] = strdup(de->d_name);
        }
        closedir(d);
    }

    if (n == 0) {
        snprintf(err, errlen, "no session '%s'", id);
        return 1;
    }
    if (n > 1) {
        char list[256];
        size_t used = 0;
        list[0] = '\0';
        for (int i = 0; i < n && used < sizeof(list) - 8; i++) {
            int w = snprintf(list + used, sizeof(list) - used,
                             "%s%.8s…", i ? ", " : "", matches[i]);
            if (w < 0) break;
            used += (size_t)w;
        }
        snprintf(err, errlen, "ambiguous id '%s' (%s)", id, list);
        for (int i = 0; i < n; i++) free(matches[i]);
        return 2;
    }
    snprintf(out, outlen, "%s", matches[0]);
    free(matches[0]);
    return 0;
}

/* ---- Reader (§4.1): session.jsonl → in-memory pi messages, trimmed ---- */

/* Parse one entry line; returns a deep copy of its message object, or NULL
   when the line is not a well-formed message entry. */
static cJSON *pi_entry_message(const char *line)
{
    cJSON *obj = cJSON_Parse(line);
    if (!obj) return NULL;
    cJSON *type = cJSON_GetObjectItemCaseSensitive(obj, "type");
    if (!type || !cJSON_IsString(type) || strcmp(type->valuestring, "message") != 0) {
        cJSON_Delete(obj);
        return NULL;
    }
    cJSON *msg = cJSON_GetObjectItemCaseSensitive(obj, "message");
    cJSON *role = msg ? cJSON_GetObjectItemCaseSensitive(msg, "role") : NULL;
    if (!msg || !cJSON_IsObject(msg) || !role || !cJSON_IsString(role)) {
        cJSON_Delete(obj);
        return NULL;
    }
    cJSON *dup = cJSON_Duplicate(msg, 1);
    cJSON_Delete(obj);
    return dup;
}

int session_load_pi(const char *session_path, cJSON *messages)
{
    FILE *f = fopen(session_path, "r");
    if (!f) return -1;

    char line[65536];
    int lineno = 0;
    int header_ok = 0;
    cJSON *all = cJSON_CreateArray();

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
        lineno++;
        if (lineno == 1) {
            /* Validate: first line is the v3 session header */
            cJSON *obj = cJSON_Parse(line);
            if (obj) {
                cJSON *type = cJSON_GetObjectItemCaseSensitive(obj, "type");
                cJSON *ver = cJSON_GetObjectItemCaseSensitive(obj, "version");
                if (type && cJSON_IsString(type) &&
                    strcmp(type->valuestring, "session") == 0 &&
                    ver && cJSON_IsNumber(ver) && ver->valueint == 3)
                    header_ok = 1;
                cJSON_Delete(obj);
            }
            if (!header_ok) {
                cJSON_Delete(all);
                fclose(f);
                return -1;
            }
            continue;
        }
        cJSON *m = pi_entry_message(line);
        if (m) cJSON_AddItemToArray(all, m);
    }
    fclose(f);

    /* A pi session must have its v3 header — an empty file is invalid, and
       so is one whose first line is not the header (§4.1 step 2). */
    if (!header_ok) {
        cJSON_Delete(all);
        return -1;
    }

    /* Trim the dangling tail: keep through the last complete assistant
       message — stopReason "stop", or "toolUse" with every toolCall id
       matched by a following toolResult. Everything after it is dropped;
       with no complete assistant message nothing is kept (§4.1 step 4). */
    int n = cJSON_GetArraySize(all);
    int keep = 0;
    for (int i = 0; i < n; i++) {
        cJSON *m = cJSON_GetArrayItem(all, i);
        cJSON *role = cJSON_GetObjectItemCaseSensitive(m, "role");
        if (!role || !cJSON_IsString(role) || strcmp(role->valuestring, "assistant") != 0)
            continue;
        cJSON *stop = cJSON_GetObjectItemCaseSensitive(m, "stopReason");
        if (!stop || !cJSON_IsString(stop)) continue;
        if (strcmp(stop->valuestring, "stop") == 0) {
            keep = i + 1;
        } else if (strcmp(stop->valuestring, "toolUse") == 0) {
            /* Complete iff it carries toolCall blocks and every id has a
               toolResult after it — a toolUse with no calls is malformed
               and never counts as a completed turn */
            int complete = 1;
            int n_calls = 0;
            int last_res = i;
            cJSON *content = cJSON_GetObjectItemCaseSensitive(m, "content");
            if (content && cJSON_IsArray(content)) {
                int nblocks = cJSON_GetArraySize(content);
                for (int b = 0; b < nblocks; b++) {
                    cJSON *blk = cJSON_GetArrayItem(content, b);
                    cJSON *bt = cJSON_GetObjectItemCaseSensitive(blk, "type");
                    if (!bt || !cJSON_IsString(bt) || strcmp(bt->valuestring, "toolCall") != 0)
                        continue;
                    cJSON *bid = cJSON_GetObjectItemCaseSensitive(blk, "id");
                    if (!bid || !cJSON_IsString(bid)) { complete = 0; break; }
                    n_calls++;
                    int found = 0;
                    for (int j = i + 1; j < n; j++) {
                        cJSON *jm = cJSON_GetArrayItem(all, j);
                        cJSON *jr = cJSON_GetObjectItemCaseSensitive(jm, "role");
                        if (!jr || !cJSON_IsString(jr) || strcmp(jr->valuestring, "toolResult") != 0)
                            continue;
                        cJSON *jt = cJSON_GetObjectItemCaseSensitive(jm, "toolCallId");
                        if (jt && cJSON_IsString(jt) &&
                            strcmp(jt->valuestring, bid->valuestring) == 0) {
                            found = 1;
                            if (j > last_res) last_res = j;
                        }
                    }
                    if (!found) { complete = 0; break; }
                }
            }
            if (complete && n_calls > 0) keep = last_res + 1;
        }
    }

    /* Move the kept prefix into the caller's array; drop the tail */
    for (int i = 0; i < n; i++) {
        cJSON *m = cJSON_DetachItemFromArray(all, 0);
        if (i < keep)
            cJSON_AddItemToArray(messages, m);
        else
            cJSON_Delete(m);
    }
    cJSON_Delete(all);
    return keep;
}

int session_write_header(jb_session *sess, const char *cwd)
{
    cJSON *h = cJSON_CreateObject();
    if (!h) return -1;

    char ts[40];
    jb_iso8601_ms(ts, sizeof(ts));

    cJSON_AddStringToObject(h, "type", "session");
    cJSON_AddNumberToObject(h, "version", 3);
    cJSON_AddStringToObject(h, "id", sess->uuid);
    cJSON_AddStringToObject(h, "timestamp", ts);
    cJSON_AddStringToObject(h, "cwd", cwd);
    /* parentSession: present only when --fork set the parent (§3.1) */
    if (sess->parent_path[0])
        cJSON_AddStringToObject(h, "parentSession", sess->parent_path);

    char *s = cJSON_PrintUnformatted(h);
    cJSON_Delete(h);
    if (!s) return -1;

    int rc = session_append_pi(sess, s);
    if (rc == 0) rc = session_append_event(sess, s);
    free(s);
    return rc;
}

/* ---- Timestamps ---- */

void jb_id8(char *out, size_t outlen)
{
    FILE *f = fopen("/dev/urandom", "rb");
    unsigned char b[4];
    if (!f || fread(b, 1, 4, f) != 4) {
        if (f) fclose(f);
        snprintf(out, outlen, "%08lx", (unsigned long)rand());
        return;
    }
    fclose(f);
    snprintf(out, outlen, "%02x%02x%02x%02x", b[0], b[1], b[2], b[3]);
}

void jb_iso8601_ms(char *out, size_t outlen)
{
    /* Real milliseconds (reference §3.1): clock_gettime, not time(NULL)
       — time(NULL) has whole-second resolution, so the .mmm field would
       be seconds-mod-1000 and same-second entries would collide. */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *gmt = gmtime(&ts.tv_sec);
    /* strftime "%Y-%m-%dT%H:%M:%S" + ".%03ldZ" */
    strftime(out, outlen - 8, "%Y-%m-%dT%H:%M:%S", gmt);
    size_t len = strlen(out);
    snprintf(out + len, outlen - len, ".%03ldZ", ts.tv_nsec / 1000000L);
}

long jb_epoch_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* ---- Metadata (the jb index) ---- */

/* Atomic write: temp file + rename — readers never see a partial file */
static int write_metadata_file(const char *path, const char *json)
{
    char tmp[4300];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    fprintf(f, "%s\n", json);
    fclose(f);
    return rename(tmp, path);
}

/* Subject: the prompt's first line, capped at the buffer size */
static void make_subject(const char *prompt, char *out, size_t outlen)
{
    const char *nl = strchr(prompt, '\n');
    size_t len = nl ? (size_t)(nl - prompt) : strlen(prompt);
    if (len > outlen - 1) len = outlen - 1;
    memcpy(out, prompt, len);
    out[len] = '\0';
}

static void add_config_snapshot(cJSON *obj, const jb_config *cfg)
{
    cJSON *c = cJSON_CreateObject();
    cJSON_AddStringToObject(c, "api_url", cfg->api_url);
    cJSON_AddStringToObject(c, "model", cfg->model);
    cJSON_AddNumberToObject(c, "max_tokens", cfg->max_tokens);
    cJSON_AddNumberToObject(c, "max_output_lines", cfg->max_output_lines);
    cJSON_AddNumberToObject(c, "max_output_bytes", cfg->max_output_bytes);
    cJSON_AddItemToObject(obj, "config", c);
}

/* Fields present from the first write: identity, lineage, config snapshot */
static cJSON *metadata_base(const jb_session *sess)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "uuid", sess->uuid);
    cJSON_AddStringToObject(m, "subject", sess->subject);
    cJSON_AddStringToObject(m, "body", "");
    cJSON_AddStringToObject(m, "author", sess->author);
    /* parent: the context link — set only by --fork (§6) */
    if (sess->parent[0])
        cJSON_AddStringToObject(m, "parent", sess->parent);
    cJSON_AddStringToObject(m, "started_at", sess->started_at);
    cJSON_AddStringToObject(m, "working_dir", sess->working_dir);
    add_config_snapshot(m, &sess->cfg_snapshot);
    return m;
}

int session_write_metadata_init(jb_session *sess, const char *prompt,
                                const char *working_dir, const jb_config *cfg)
{
    make_subject(prompt, sess->subject, sizeof(sess->subject));
    snprintf(sess->working_dir, sizeof(sess->working_dir), "%s", working_dir);
    sess->cfg_snapshot = *cfg;
    jb_iso8601_ms(sess->started_at, sizeof(sess->started_at));

    cJSON *m = metadata_base(sess);
    cJSON_AddStringToObject(m, "status", "working");
    cJSON_AddStringToObject(m, "last_activity", sess->started_at);

    char *s = cJSON_PrintUnformatted(m);
    cJSON_Delete(m);
    if (!s) return -1;
    int rc = write_metadata_file(sess->metadata_path, s);
    free(s);
    return rc;
}

int session_write_metadata_close(jb_session *sess, const char *status,
                                 long tokens_used, int turns, int exit_code)
{
    char ended_at[40];
    jb_iso8601_ms(ended_at, sizeof(ended_at));

    cJSON *m = metadata_base(sess);
    cJSON_AddStringToObject(m, "status", status);
    cJSON_AddStringToObject(m, "ended_at", ended_at);
    cJSON_AddNumberToObject(m, "turns", turns);
    cJSON_AddNumberToObject(m, "tokens_used", tokens_used);
    cJSON_AddNumberToObject(m, "exit_code", exit_code);
    cJSON_AddStringToObject(m, "last_activity", ended_at);

    char *s = cJSON_PrintUnformatted(m);
    cJSON_Delete(m);
    if (!s) return -1;
    int rc = write_metadata_file(sess->metadata_path, s);
    free(s);
    return rc;
}

/* Read a whole file into a malloc'd NUL-terminated buffer (local copy —
   meta.c's jb_read_file is a verb-layer helper). */
static char *read_file_local(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    long sz;
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

/* Heartbeat (reference §6): rewrite metadata.json with a fresh
   last_activity while the session is working. Read-modify-write, atomic
   (temp + rename) — callers run one heartbeat per completed turn. */
int session_write_metadata_heartbeat(jb_session *sess)
{
    char *json = read_file_local(sess->metadata_path);
    if (!json) return -1;
    cJSON *m = cJSON_Parse(json);
    free(json);
    if (!m) return -1;

    char now[40];
    jb_iso8601_ms(now, sizeof(now));
    cJSON_AddStringToObject(m, "last_activity", now);

    char *s = cJSON_PrintUnformatted(m);
    cJSON_Delete(m);
    if (!s) return -1;
    int rc = write_metadata_file(sess->metadata_path, s);
    free(s);
    return rc;
}

void session_close(jb_session *sess)
{
    if (sess->session_fp) { fclose(sess->session_fp); sess->session_fp = NULL; }
    if (sess->events_fp) { fclose(sess->events_fp); sess->events_fp = NULL; }
    for (int i = 0; i < sess->used_n; i++) free(sess->used_ids[i]);
    free(sess->used_ids);
    sess->used_ids = NULL;
    sess->used_n = 0;
    sess->used_cap = 0;
}
