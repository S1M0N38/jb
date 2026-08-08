/* session.c — session storage: .jb/sessions/<uuid>/{metadata,session,events}.json */
#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
    time_t now = time(NULL);
    struct tm *gmt = gmtime(&now);
    /* strftime "%Y-%m-%dT%H:%M:%S" + ".%03ldZ" */
    strftime(out, outlen - 8, "%Y-%m-%dT%H:%M:%S", gmt);
    size_t len = strlen(out);
    snprintf(out + len, outlen - len, ".%03ldZ", now % 1000);
}

long jb_epoch_ms(void)
{
    return (long)time(NULL) * 1000L;
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

void session_close(jb_session *sess)
{
    if (sess->session_fp) { fclose(sess->session_fp); sess->session_fp = NULL; }
    if (sess->events_fp) { fclose(sess->events_fp); sess->events_fp = NULL; }
}
