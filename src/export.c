/* export.c — jb export ID [PATH] (plan phase 8): the pi viewer.
   The session file IS the JSONL export — byte-equivalent copy for
   .jsonl, directly importable via pi's /import. The HTML export is the
   pi viewer: the vendored template (pi 0.84.1, MIT — src/assets.inc,
   generated at build time from src/vendor/pi-export/) with the payload
   {header, entries, leafId, systemPrompt, tools} base64'd into
   {{SESSION_DATA}}. Working sessions are exported with snapshot
   semantics — read-only, no lock. */
#include "export.h"
#include "meta.h"
#include "session.h"
#include "prompt.h"
#include "tools.h"
#include "cJSON.h"
#include "assets.inc"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- base64 (RFC 4648, no line breaks) ---- */

static void b64_encode(const unsigned char *in, size_t inlen, char *out)
{
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i, o = 0;
    for (i = 0; i + 2 < inlen; i += 3) {
        unsigned v = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
        out[o++] = tbl[(v >> 18) & 63];
        out[o++] = tbl[(v >> 12) & 63];
        out[o++] = tbl[(v >> 6) & 63];
        out[o++] = tbl[v & 63];
    }
    if (i < inlen) {
        unsigned v = in[i] << 16;
        if (i + 1 < inlen) v |= in[i + 1] << 8;
        out[o++] = tbl[(v >> 18) & 63];
        out[o++] = tbl[(v >> 12) & 63];
        out[o++] = (i + 1 < inlen) ? tbl[(v >> 6) & 63] : '=';
        out[o++] = '=';
    }
    out[o] = '\0';
}

/* ---- placeholder replacement (each placeholder appears once) ---- */

/* replace_placeholder — find {{NAME}} in buf[0..len) and splice repl in.
   Returns the new length, or -1 when the placeholder is missing. */
static long replace_placeholder(char *buf, size_t len, const char *name,
                                const char *repl, size_t repl_len)
{
    char ph[64];
    snprintf(ph, sizeof(ph), "{{%s}}", name);
    size_t ph_len = strlen(ph);
    char *at = NULL;
    for (size_t i = 0; i + ph_len <= len; i++) {
        if (memcmp(buf + i, ph, ph_len) == 0) { at = buf + i; break; }
    }
    if (!at) return -1;
    size_t tail = len - (at - buf) - ph_len;
    if (repl_len != ph_len)
        memmove(at + repl_len, at + ph_len, tail);
    if (repl_len)
        memcpy(at, repl, repl_len);
    return (long)len + (long)repl_len - (long)ph_len;
}

/* splice_asset — replace {{NAME}} in the growing output buffer with one
   embedded asset. Returns 0, or -1 when the placeholder is missing. */
static int splice_asset(char *buf, size_t *len,
                        const char *name, const unsigned char *data,
                        size_t data_len)
{
    long nl = replace_placeholder(buf, *len, name,
                                  (const char *)data, data_len);
    if (nl < 0) return -1;
    buf[nl] = '\0';
    *len = (size_t)nl;
    return 0;
}

/* write_file — write buf to path (shared by the .jsonl and .html
   branches). Returns 0 on success, -1 with a message on stderr. */
static int write_file(const char *path, const char *buf, size_t len);

/* ---- HTML export ---- */

/* build_payload — the pi /export payload: {header, entries, leafId,
   systemPrompt, tools} (reference §8.2). Parses session.jsonl line by
   line: line 1 is the header, every following line an entry. Returns
   the cJSON object (caller frees), or NULL on a hard failure. */
static cJSON *build_payload(const char *session_path)
{
    char *json = jb_read_file(session_path);
    if (!json) return NULL;

    cJSON *payload = cJSON_CreateObject();
    cJSON *entries = cJSON_CreateArray();
    cJSON *header = NULL;
    cJSON *leaf = NULL;

    char *line = json;
    int first = 1;
    int skipped = 0;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        cJSON *obj = cJSON_Parse(line);
        if (obj) {
            if (first) {
                header = obj;
                first = 0;
            } else {
                cJSON *id = cJSON_GetObjectItemCaseSensitive(obj, "id");
                if (cJSON_IsString(id) && id->valuestring)
                    leaf = id;
                cJSON_AddItemToArray(entries, obj);
            }
        } else if (line[0]) {
            skipped++;  /* torn tail line from a concurrently-written
                           working session — snapshot semantics, warn */
        }
        if (!nl) break;
        line = nl + 1;
    }
    free(json);

    if (skipped)
        fprintf(stderr, "jb: %d unparsable line(s) omitted from the export\n",
                skipped);

    if (!header) {
        cJSON_Delete(payload);
        cJSON_Delete(entries);
        return NULL;
    }

    cJSON_AddItemToObject(payload, "header", header);
    cJSON_AddItemToObject(payload, "entries", entries);
    if (leaf) {
        cJSON *leaf_copy = cJSON_Duplicate(leaf, 1);
        cJSON_AddItemToObject(payload, "leafId", leaf_copy);
    } else {
        cJSON_AddNullToObject(payload, "leafId");
    }

    char *sp = prompt_build();
    cJSON_AddStringToObject(payload, "systemPrompt", sp ? sp : "");
    free(sp);

    cJSON *tools = tools_get_definitions();
    cJSON_AddItemToObject(payload, "tools", tools);
    return payload;
}

/* export_html — assemble the self-contained viewer (reference §8.3):
   theme vars + the three export colors are hardcoded constants (jb has
   no TUI themes — always the default dark theme). Returns 0 on success,
   -1 on failure (with errno-style message on stderr). */
static int export_html(const char *session_path, const char *out_path)
{
    cJSON *payload = build_payload(session_path);
    if (!payload) {
        fprintf(stderr, "jb: cannot read %s\n", session_path);
        return -1;
    }
    char *json = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (!json) return -1;

    size_t b64_len = 4 * ((strlen(json) + 2) / 3) + 1;
    char *b64 = malloc(b64_len);
    if (!b64) { free(json); return -1; }
    b64_encode((const unsigned char *)json, strlen(json), b64);
    free(json);

    /* {{CSS}} = template.css with the theme placeholders resolved */
    size_t css_cap = jb_asset_template_css_len + jb_asset_theme_vars_css_len
                     + 64;
    char *css = malloc(css_cap);
    if (!css) { free(b64); return -1; }
    memcpy(css, jb_asset_template_css, jb_asset_template_css_len);
    size_t css_len = jb_asset_template_css_len;
    css[css_len] = '\0';

    static const struct {
        const char *name;
        const unsigned char *value;
        size_t len;
    } css_theme[] = {
        { "THEME_VARS", jb_asset_theme_vars_css, jb_asset_theme_vars_css_len },
        { "BODY_BG",     (const unsigned char *)"#18181e", 7 },
        { "CONTAINER_BG", (const unsigned char *)"#1e1e24", 7 },
        { "INFO_BG",     (const unsigned char *)"#3c3728", 7 }
    };
    int fail = 0;
    for (size_t i = 0; i < sizeof(css_theme) / sizeof(css_theme[0]); i++) {
        if (splice_asset(css, &css_len, css_theme[i].name,
                         css_theme[i].value, css_theme[i].len) != 0)
            fail = 1;
    }
    if (fail) { free(css); free(b64); return -1; }

    /* the final document: template.html with the five placeholders */
    size_t cap = jb_asset_template_html_len + css_len
                 + jb_asset_template_js_len + jb_asset_vendor_marked_min_js_len
                 + jb_asset_vendor_highlight_min_js_len + b64_len + 16;
    char *html = malloc(cap);
    if (!html) { free(css); free(b64); return -1; }
    memcpy(html, jb_asset_template_html, jb_asset_template_html_len);
    size_t html_len = jb_asset_template_html_len;
    html[html_len] = '\0';

    if (splice_asset(html, &html_len, "CSS",
                     (const unsigned char *)css, css_len) != 0)
        fail = 1;
    if (splice_asset(html, &html_len, "JS",
                     jb_asset_template_js, jb_asset_template_js_len) != 0)
        fail = 1;
    if (splice_asset(html, &html_len, "MARKED_JS",
                     jb_asset_vendor_marked_min_js,
                     jb_asset_vendor_marked_min_js_len) != 0)
        fail = 1;
    if (splice_asset(html, &html_len, "HIGHLIGHT_JS",
                     jb_asset_vendor_highlight_min_js,
                     jb_asset_vendor_highlight_min_js_len) != 0)
        fail = 1;
    if (splice_asset(html, &html_len, "SESSION_DATA",
                     (const unsigned char *)b64, strlen(b64)) != 0)
        fail = 1;
    free(css);
    free(b64);

    if (fail) {
        fprintf(stderr, "jb: export template is missing placeholders\n");
        free(html);
        return -1;
    }
    int rc = write_file(out_path, html, html_len);
    free(html);
    return rc;
}

/* write_file — write buf to path (shared by the .jsonl and .html
   branches). Returns 0 on success, -1 with a message on stderr. */
static int write_file(const char *path, const char *buf, size_t len)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "jb: cannot write %s\n", path);
        return -1;
    }
    size_t n = fwrite(buf, 1, len, f);
    int rc = (n == len) ? 0 : -1;
    if (fclose(f) != 0) rc = -1;
    if (rc != 0)
        fprintf(stderr, "jb: cannot write %s\n", path);
    return rc;
}

/* ---- Command ---- */

int cmd_export(int argc, char **argv)
{
    const char *id_arg = NULL;
    const char *path_arg = NULL;

    for (int i = 0; i < argc; i++) {
        if (argv[i][0] == '-') {
            fprintf(stderr, "jb: unknown option '%s' for 'export'\n", argv[i]);
            return 2;
        } else if (!id_arg) {
            id_arg = argv[i];
        } else if (!path_arg) {
            path_arg = argv[i];
        } else {
            fprintf(stderr, "jb: too many arguments for 'export' (see 'jb help export')\n");
            return 2;
        }
    }
    if (!id_arg) {
        fprintf(stderr, "jb: export requires a session ID (see 'jb help export')\n");
        return 2;
    }

    /* resolve the repository: walk up from cwd (fatal outside any repo) */
    char cwd_buf[4096];
    const char *cwd = getcwd(cwd_buf, sizeof(cwd_buf)) ? cwd_buf : ".";
    char repo_root[4096];
    if (jb_find_repo(cwd, repo_root, sizeof(repo_root)) != 0) {
        fprintf(stderr, "jb: fatal: not a jb repository (run 'jb init')\n");
        return 1;
    }

    char uuid[JB_UUID_LEN];
    if (jb_resolve_id_arg(repo_root, id_arg, uuid, sizeof(uuid)) != 0)
        return 1;

    char session_path[4096];
    snprintf(session_path, sizeof(session_path),
             "%s/.jb/sessions/%s/session.jsonl", repo_root, uuid);

    /* the extension decides the format: .jsonl → copy, else → HTML */
    int is_jsonl = 0;
    if (path_arg) {
        size_t plen = strlen(path_arg);
        is_jsonl = plen >= 6 && strcmp(path_arg + plen - 6, ".jsonl") == 0;
    }

    char default_path[4600];
    if (!path_arg) {
        snprintf(default_path, sizeof(default_path),
                 "jb-session-%s.html", uuid);
        path_arg = default_path;
    }

    int rc;
    if (is_jsonl) {
        char *json = jb_read_file(session_path);
        if (!json) {
            fprintf(stderr, "jb: cannot read %s\n", session_path);
            return 1;
        }
        rc = write_file(path_arg, json, strlen(json));
        free(json);
    } else {
        rc = export_html(session_path, path_arg);
    }
    if (rc != 0)
        return 1;

    char sid[9];
    jb_short_id(uuid, sid, sizeof(sid));
    printf("jb: exported %s → %s\n", sid, path_arg);
    return 0;
}
