/* session.c — session state management (UUID, dirs, JSONL files) */
#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

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

    if (generate_uuid(sess->uuid, sizeof(sess->uuid)) != 0)
        return -1;

    snprintf(sess->session_dir, sizeof(sess->session_dir),
        "%s/jb/sessions/%s", xdg_cache_home(), sess->uuid);

    snprintf(sess->state_path, sizeof(sess->state_path),
        "%s/state.jsonl", sess->session_dir);

    snprintf(sess->log_path, sizeof(sess->log_path),
        "%s/log.jsonl", sess->session_dir);

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

void session_close(jb_session *sess)
{
    if (sess->log_fp) { fclose(sess->log_fp); sess->log_fp = NULL; }
    if (sess->state_fp) { fclose(sess->state_fp); sess->state_fp = NULL; }
}
