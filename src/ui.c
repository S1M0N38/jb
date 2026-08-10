/* ui.c — jb ui: the read-only session forest viewer (localhost HTTP).
   A tiny single-threaded HTTP server on 127.0.0.1. Serves the embedded
   ui/ assets (src/ui_assets.inc, generated from ui/ at build time) and
   two JSON endpoints that read the sessions' metadata.json files — nothing
   else: no writes, no provider calls, no config. The browser polls
   /api/sessions every 2s; the terminal stays the controller. */
#include "ui.h"
#include "meta.h"
#include "cJSON.h"
#include "ui_assets.inc"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define UI_DEFAULT_PORT 8787
#define UI_USAGE "usage: jb ui [--port N] [--dev]\n"

/* ---- tiny HTTP plumbing ---- */

/* send_all — write the whole buffer; the peer is localhost, so partial
   writes are a hard error. */
static void send_all(int cfd, const void *buf, size_t len)
{
    const char *p = buf;
    while (len > 0) {
        ssize_t n = send(cfd, p, len, 0);
        if (n <= 0) return;
        p += n;
        len -= (size_t)n;
    }
}

/* respond — one HTTP/1.1 response, connection closed after. */
static void respond(int cfd, int status, const char *reason,
                    const char *ctype, const char *body, size_t len)
{
    char head[512];
    int hl = snprintf(head, sizeof(head),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, reason, ctype, len);
    send_all(cfd, head, (size_t)hl);
    if (len) send_all(cfd, body, len);
}

static void respond_err(int cfd, int status, const char *reason,
                        const char *msg)
{
    respond(cfd, status, reason, "text/plain; charset=utf-8", msg,
            strlen(msg));
}

static void respond_embedded(int cfd, const unsigned char *data,
                             unsigned int len, const char *ctype)
{
    respond(cfd, 200, "OK", ctype, (const char *)data, len);
}

/* serve_disk — --dev: read <repo_root>/ui/<name> and serve it; 404 when
   missing. */
static void serve_disk(int cfd, const char *repo_root, const char *name,
                       const char *ctype)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/ui/%s", repo_root, name);
    char *body = jb_read_file(path);
    if (!body) {
        respond_err(cfd, 404, "Not Found", "not found\n");
        return;
    }
    respond(cfd, 200, "OK", ctype, body, strlen(body));
    free(body);
}

/* ---- JSON endpoints ---- */

/* sessions_json — every session's metadata as a JSON array. The card
   fields only: the detail endpoint carries the full record. */
static char *sessions_json(const char *repo_root)
{
    session_list list = {0};
    session_list_scan(repo_root, &list);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < list.n; i++) {
        const session_rec *r = &list.items[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "uuid", r->uuid);
        cJSON_AddStringToObject(o, "status", r->status);
        cJSON_AddStringToObject(o, "subject", r->subject);
        cJSON_AddStringToObject(o, "author", r->author);
        cJSON_AddStringToObject(o, "parent", r->parent);
        cJSON_AddNumberToObject(o, "started_ms", (double)r->started_ms);
        if (r->has_ended)
            cJSON_AddNumberToObject(o, "ended_ms", (double)r->ended_ms);
        cJSON_AddNumberToObject(o, "exit_code", (double)r->exit_code);
        cJSON_AddItemToArray(arr, o);
    }
    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    session_list_free(&list);
    return out;
}

/* session_detail — the raw metadata.json of one session. The id must be
   an exact uuid of a scanned session (validates existence); returns NULL
   otherwise. */
static char *session_detail(const char *repo_root, const char *id)
{
    session_list list = {0};
    session_list_scan(repo_root, &list);
    int found = 0;
    for (int i = 0; i < list.n; i++) {
        if (strcmp(list.items[i].uuid, id) == 0) { found = 1; break; }
    }
    if (!found) {
        session_list_free(&list);
        return NULL;
    }
    session_list_free(&list);

    char meta[4096];
    snprintf(meta, sizeof(meta), "%s/.jb/sessions/%s/metadata.json",
             repo_root, id);
    return jb_read_file(meta);
}

/* ---- request handling ---- */

static void handle_client(int cfd, const char *repo_root, int dev)
{
    struct timeval tv = { 5, 0 };
    setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char req[8192];
    ssize_t n = recv(cfd, req, sizeof(req) - 1, 0);
    if (n <= 0) return;
    req[n] = '\0';

    char method[8], path[1025];
    if (sscanf(req, "%7s %1024s", method, path) != 2) {
        respond_err(cfd, 400, "Bad Request", "bad request\n");
        return;
    }
    if (strcmp(method, "GET") != 0) {
        respond_err(cfd, 405, "Method Not Allowed",
                    "method not allowed\n");
        return;
    }
    char *q = strchr(path, '?');
    if (q) *q = '\0';

    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        if (dev) serve_disk(cfd, repo_root, "index.html",
                            "text/html; charset=utf-8");
        else respond_embedded(cfd, jb_ui_index_html, jb_ui_index_html_len,
                              "text/html; charset=utf-8");
        return;
    }
    if (strcmp(path, "/style.css") == 0) {
        if (dev) serve_disk(cfd, repo_root, "style.css",
                            "text/css; charset=utf-8");
        else respond_embedded(cfd, jb_ui_style_css, jb_ui_style_css_len,
                              "text/css; charset=utf-8");
        return;
    }
    if (strcmp(path, "/app.js") == 0) {
        if (dev) serve_disk(cfd, repo_root, "app.js",
                            "application/javascript; charset=utf-8");
        else respond_embedded(cfd, jb_ui_app_js, jb_ui_app_js_len,
                              "application/javascript; charset=utf-8");
        return;
    }
    if (strcmp(path, "/api/sessions") == 0) {
        char *json = sessions_json(repo_root);
        if (json) {
            respond(cfd, 200, "OK", "application/json", json, strlen(json));
            free(json);
        } else {
            respond_err(cfd, 500, "Internal Server Error", "oops\n");
        }
        return;
    }
    if (strncmp(path, "/api/session/", 13) == 0) {
        const char *id = path + 13;
        if (id[0]) {
            char *meta = session_detail(repo_root, id);
            if (meta) {
                respond(cfd, 200, "OK", "application/json", meta,
                        strlen(meta));
                free(meta);
                return;
            }
        }
        respond_err(cfd, 404, "Not Found", "not found\n");
        return;
    }

    respond_err(cfd, 404, "Not Found", "not found\n");
}

/* ---- serve loop ---- */

static int ui_serve(const char *repo_root, int port, int dev)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "jb: ui: socket: %s\n", strerror(errno));
        return 1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(0x7f000001);   /* 127.0.0.1 — localhost only */
    sa.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        fprintf(stderr, "jb: ui: cannot bind 127.0.0.1:%d (%s)\n",
                port, strerror(errno));
        close(fd);
        return 1;
    }
    if (listen(fd, 8) != 0) {
        fprintf(stderr, "jb: ui: listen: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    socklen_t sl = sizeof(sa);
    getsockname(fd, (struct sockaddr *)&sa, &sl);
    int actual = ntohs(sa.sin_port);

    printf("jb ui: http://127.0.0.1:%d/  (Ctrl-C to stop)\n", actual);
    fflush(stdout);

    /* Auto-open the browser only when stdout is a terminal (interactive
       use); redirected output (tests, logs) never spawns anything. */
    if (isatty(STDOUT_FILENO)) {
        char url[64];
        snprintf(url, sizeof(url), "http://127.0.0.1:%d/", actual);
        const char *cmd = NULL;
        if (access("/usr/bin/open", X_OK) == 0) cmd = "open";
        else if (access("/usr/bin/xdg-open", X_OK) == 0) cmd = "xdg-open";
        if (cmd) {
            pid_t pid = fork();
            if (pid == 0) {
                execlp(cmd, cmd, url, (char *)NULL);
                _exit(127);
            }
        }
    }

    for (;;) {
        int cfd = accept(fd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "jb: ui: accept: %s\n", strerror(errno));
            close(fd);
            return 1;
        }
        handle_client(cfd, repo_root, dev);
        close(cfd);
    }
}

/* ---- command ---- */

int cmd_ui(int argc, char **argv)
{
    int port = UI_DEFAULT_PORT;
    int dev = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "jb: option '--port' requires a value\n");
                return 2;
            }
            const char *s = argv[++i];
            char *end = NULL;
            long v = strtol(s, &end, 10);
            if (!s[0] || (end && *end) || v < 0 || v > 65535) {
                fprintf(stderr, "jb: invalid port '%s'\n", s);
                return 2;
            }
            port = (int)v;
        } else if (strcmp(argv[i], "--dev") == 0) {
            dev = 1;
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            printf(UI_USAGE);
            return 0;
        } else {
            fprintf(stderr, "jb: unknown option '%s' for 'ui'\n", argv[i]);
            return 2;
        }
    }

    char cwd_buf[4096];
    const char *cwd = getcwd(cwd_buf, sizeof(cwd_buf)) ? cwd_buf : ".";
    char repo_root[4096];
    if (jb_find_repo(cwd, repo_root, sizeof(repo_root)) != 0) {
        fprintf(stderr, "jb: fatal: not a jb repository (run 'jb init')\n");
        return 1;
    }

    return ui_serve(repo_root, port, dev);
}
