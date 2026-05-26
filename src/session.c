/* session.c — session state management (UUID, dirs, JSONL files) */
#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

static const char *xdg_cache_home(void)
{
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg && xdg[0]) return xdg;
    static char buf[4096];
    snprintf(buf, sizeof(buf), "%s/.cache", getenv("HOME"));
    return buf;
}

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

int session_init(jb_session *sess)
{
    memset(sess, 0, sizeof(*sess));
    sess->parent[0] = '\0';

    if (generate_uuid(sess->uuid, sizeof(sess->uuid)) != 0)
        return -1;

    snprintf(sess->session_dir, sizeof(sess->session_dir),
        "%s/jb/sessions/%s", xdg_cache_home(), sess->uuid);

    snprintf(sess->state_path, sizeof(sess->state_path),
        "%s/state.jsonl", sess->session_dir);

    snprintf(sess->log_path, sizeof(sess->log_path),
        "%s/log.jsonl", sess->session_dir);

    snprintf(sess->metadata_path, sizeof(sess->metadata_path),
        "%s/metadata.json", sess->session_dir);

    /* Create session directory */
    if (mkdirs(sess->session_dir) != 0)
        return -1;

    /* Open log file for appending */
    sess->log_fp = fopen(sess->log_path, "a");
    if (!sess->log_fp) return -1;

    /* Open state file for appending */
    sess->state_fp = fopen(sess->state_path, "a");
    if (!sess->state_fp) {
        fclose(sess->log_fp);
        return -1;
    }

    return 0;
}

int session_append_state(jb_session *sess, const char *json_line)
{
    if (!sess->state_fp) return -1;
    fprintf(sess->state_fp, "%s\n", json_line);
    fflush(sess->state_fp);
    return 0;
}

int session_append_log(jb_session *sess, const char *line)
{
    if (!sess->log_fp) return -1;
    fprintf(sess->log_fp, "%s\n", line);
    fflush(sess->log_fp);
    return 0;
}

void session_set_parent(jb_session *sess, const char *parent_uuid)
{
    if (parent_uuid && parent_uuid[0]) {
        strncpy(sess->parent, parent_uuid, JB_UUID_LEN - 1);
        sess->parent[JB_UUID_LEN - 1] = '\0';
    }
}

void session_close(jb_session *sess)
{
    if (sess->log_fp) { fclose(sess->log_fp); sess->log_fp = NULL; }
    if (sess->state_fp) { fclose(sess->state_fp); sess->state_fp = NULL; }
}

/* ---- Helpers ---- */

/* Generate ISO 8601 UTC timestamp: "2026-05-25T20:12:00Z" */
static void iso8601_now(char *out, size_t outlen)
{
    time_t now = time(NULL);
    struct tm *gmt = gmtime(&now);
    strftime(out, outlen, "%Y-%m-%dT%H:%M:%SZ", gmt);
}

/* Truncate prompt to ~60 chars at word boundary for title */
static void make_title(const char *prompt, char *out, size_t outlen)
{
    size_t max = 60;
    /* Take first line only */
    const char *nl = strchr(prompt, '\n');
    size_t len = nl ? (size_t)(nl - prompt) : strlen(prompt);

    if (len <= max) {
        snprintf(out, outlen, "%.*s", (int)len, prompt);
        return;
    }

    /* Cut at word boundary */
    size_t cut = max;
    while (cut > 0 && prompt[cut] != ' ') cut--;
    if (cut == 0) cut = max;  /* no space found, hard cut */

    snprintf(out, outlen, "%.*s...", (int)cut, prompt);
}

/* Write metadata.json to session directory */
static int write_metadata_file(const char *path, const char *json)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%s\n", json);
    fclose(f);
    return 0;
}

int session_write_metadata_init(jb_session *sess, const char *prompt,
                                const char *working_dir, const char *model)
{
    /* Derive title from prompt */
    char title_buf[128];
    make_title(prompt, title_buf, sizeof(title_buf));

    /* Store for later use at close */
    snprintf(sess->title, sizeof(sess->title), "%s", title_buf);
    snprintf(sess->working_dir, sizeof(sess->working_dir), "%s", working_dir);
    snprintf(sess->model, sizeof(sess->model), "%s", model);

    iso8601_now(sess->started_at, sizeof(sess->started_at));

    /* Build JSON manually — no cJSON dependency for this simple flat object */
    char json[1536];
    int n;
    if (sess->parent[0]) {
        n = snprintf(json, sizeof(json),
            "{\"uuid\":\"%s\",\"parent\":\"%s\",\"status\":\"running\",\"title\":\"%s\",\"started_at\":\"%s\",\"working_dir\":\"%s\",\"model\":\"%s\"}",
            sess->uuid, sess->parent, title_buf, sess->started_at, working_dir, model);
    } else {
        n = snprintf(json, sizeof(json),
            "{\"uuid\":\"%s\",\"status\":\"running\",\"title\":\"%s\",\"started_at\":\"%s\",\"working_dir\":\"%s\",\"model\":\"%s\"}",
            sess->uuid, title_buf, sess->started_at, working_dir, model);
    }

    if (n < 0 || (size_t)n >= sizeof(json)) return -1;
    return write_metadata_file(sess->metadata_path, json);
}

int session_write_metadata_close(jb_session *sess, const char *status,
                                 long tokens_used, int turns, int exit_code)
{
    char ended_at[32];
    iso8601_now(ended_at, sizeof(ended_at));

    char json[2560];
    int n;
    if (sess->parent[0]) {
        n = snprintf(json, sizeof(json),
            "{\"uuid\":\"%s\",\"parent\":\"%s\",\"status\":\"%s\",\"title\":\"%s\",\"started_at\":\"%s\",\"ended_at\":\"%s\",\"working_dir\":\"%s\",\"model\":\"%s\",\"tokens_used\":%ld,\"turns\":%d,\"exit_code\":%d}",
            sess->uuid, sess->parent, status, sess->title, sess->started_at, ended_at,
            sess->working_dir, sess->model, tokens_used, turns, exit_code);
    } else {
        n = snprintf(json, sizeof(json),
            "{\"uuid\":\"%s\",\"status\":\"%s\",\"title\":\"%s\",\"started_at\":\"%s\",\"ended_at\":\"%s\",\"working_dir\":\"%s\",\"model\":\"%s\",\"tokens_used\":%ld,\"turns\":%d,\"exit_code\":%d}",
            sess->uuid, status, sess->title, sess->started_at, ended_at,
            sess->working_dir, sess->model, tokens_used, turns, exit_code);
    }

    if (n < 0 || (size_t)n >= sizeof(json)) return -1;
    return write_metadata_file(sess->metadata_path, json);
}
