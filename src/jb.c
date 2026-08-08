/* jb.c — main entry point and agentic loop */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cJSON.h"
#include "config.h"
#include "session.h"
#include "api.h"
#include "tools.h"
#include "prompt.h"
#include "version.h"

/* ---- Globals for signal handling ---- */
static jb_session g_session;
static int g_session_active = 0;
static char *g_partial_answer = NULL;

/* JSON-escape a string into out — allocation-free, safe for the signal
   handler. Truncates when out is nearly full. */
static void json_escape(const char *in, char *out, size_t outlen);

/* provider = hostname of api_url, reference §3.4: */
static void provider_from_url(const char *api_url, char *out, size_t outlen);

static void json_escape(const char *in, char *out, size_t outlen)
{
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 7 < outlen; p++) {
        switch (*p) {
        case '"':  out[o++] = '\\'; out[o++] = '"'; break;
        case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
        case '\n': out[o++] = '\\'; out[o++] = 'n'; break;
        case '\r': out[o++] = '\\'; out[o++] = 'r'; break;
        case '\t': out[o++] = '\\'; out[o++] = 't'; break;
        default:
            if (*p < 0x20) {
                snprintf(out + o, outlen - o, "\\u%04x", *p);
                o += 6;
            } else {
                out[o++] = (char)*p;
            }
        }
    }
    out[o] = '\0';
}

static void handle_signal(int sig)
{
    if (g_session_active) {
        /* Aborted assistant entry + interrupted metadata before dying.
           The entry line is hand-built with snprintf — a signal handler
           must not run cJSON/malloc (the interrupted code may hold the
           allocator lock). Skipped when no header has been written yet
           (the prompt never arrived — nothing to chain to).
           Caveat: session_write_metadata_close uses cJSON; in practice
           SIGINT lands while jb is blocked reading the SSE stream, where
           no allocation is in progress. */
        if (ftell(g_session.session_fp) > 0) {
            char id8[16], ts[40], ptext[4096], esc[8192];
            jb_id8(id8, sizeof(id8));
            jb_iso8601_ms(ts, sizeof(ts));
            api_stream_text_snapshot(ptext, sizeof(ptext));
            json_escape(ptext, esc, sizeof(esc));

            /* provider/model from the config snapshot — empty when the
               run never got past reading stdin (§3.3 shape) */
            char provider[128], provq[160], modelq[160];
            provider_from_url(g_session.cfg_snapshot.api_url, provider, sizeof(provider));
            if (g_session.cfg_snapshot.model[0] && provider[0]) {
                snprintf(provq, sizeof(provq), "\"provider\":\"%s\",", provider);
                snprintf(modelq, sizeof(modelq), "\"model\":\"%s\",", g_session.cfg_snapshot.model);
            } else {
                snprintf(provq, sizeof(provq), "");
                snprintf(modelq, sizeof(modelq), "");
            }

            char block[8256];
            if (esc[0]) {
                snprintf(block, sizeof(block),
                    "[{\"type\":\"text\",\"text\":\"%s\"}]", esc);
            } else {
                snprintf(block, sizeof(block), "[]");
            }

            char parentq[24];
            if (g_session.last_entry_id[0]) {
                snprintf(parentq, sizeof(parentq), "\"%s\"", g_session.last_entry_id);
            } else {
                snprintf(parentq, sizeof(parentq), "null");
            }

            char line[12288];
            snprintf(line, sizeof(line),
                "{\"type\":\"message\",\"id\":\"%s\",\"parentId\":%s,"
                "\"timestamp\":\"%s\","
                "\"message\":{\"role\":\"assistant\",\"content\":%s,"
                "%s%s"
                "\"api\":\"openai-completions\",\"stopReason\":\"aborted\","
                "\"errorMessage\":\"interrupted by signal %d\",\"timestamp\":%ld}}",
                id8, parentq, ts, block, provq, modelq, sig, jb_epoch_ms());
            session_append_raw(&g_session, line);
        }
        session_write_metadata_close(&g_session, "interrupted", 0, 0,
                                     sig == SIGINT ? 130 : 143);
        session_close(&g_session);
        g_session_active = 0;
    }
    if (g_partial_answer && g_partial_answer[0]) {
        printf("%s\n", g_partial_answer);
        fflush(stdout);
    }
    _exit(sig == SIGINT ? 130 : 143);
}

/* Read all of stdin into a malloc'd string */
static char *read_stdin(void)
{
    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;

    size_t nread;
    while ((nread = fread(buf + len, 1, cap - len - 1, stdin)) > 0) {
        len += nread;
        if (len + 1 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }
    buf[len] = '\0';
    return buf;
}

/* ---- Command implementations ---- */

/* cmd_help — the command reference; bare jb = jb help */
static void cmd_help(const char *verb)
{
    if (verb) {
        printf("usage: jb %s ...\n", verb);
        return;
    }
    printf("usage: jb <command> [<args>]\n");
    printf("\n");
    printf("commands:\n");
    printf("  init       create an empty jb repository (.jb/)\n");
    printf("  run        run the agent on a prompt from stdin\n");
    printf("  commit     finalize a completed session\n");
    printf("  status     show the current session and repo summary\n");
    printf("  log        list sessions (flat or --graph)\n");
    printf("  show       pretty-print session metadata\n");
    printf("  ps         list children of the current session\n");
    printf("  wait       wait for a session to finish\n");
    printf("  path       print a session's directory\n");
    printf("  export     export a session (HTML viewer / JSONL)\n");
    printf("  config     get/set configuration\n");
    printf("  help       show this help\n");
    printf("\n");
    printf("global flags:\n");
    printf("  -C DIR            resolve the repository from DIR\n");
    printf("  -c KEY=VALUE      config override (repeatable)\n");
    printf("  --config PATH     load config from PATH\n");
    printf("  --version         print version\n");
    printf("  --help            show this help\n");
    printf("\n");
    printf("exit codes: 0 success · 1 error/not found · 2 usage\n");
}

static int path_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int path_is_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* mkdir -p for a single path (all intermediate components) */
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

/* cmd_init — create .jb/ with sessions/ and an empty local config */
static int cmd_init(void)
{
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        fprintf(stderr, "jb: cannot determine working directory\n");
        return 1;
    }

    char jbdir[4096];
    snprintf(jbdir, sizeof(jbdir), "%s/.jb", cwd);
    int existed = path_is_dir(jbdir);

    char sessions_dir[4096];
    snprintf(sessions_dir, sizeof(sessions_dir), "%s/.jb/sessions", cwd);
    mkdirs(sessions_dir);

    /* Local config: only if absent — never clobber an existing one */
    char cfg_path[4096];
    snprintf(cfg_path, sizeof(cfg_path), "%s/.jb/config.json", cwd);
    if (!path_is_file(cfg_path)) {
        FILE *f = fopen(cfg_path, "w");
        if (f) {
            fprintf(f, "{}\n");
            fclose(f);
        }
    }

    if (existed) {
        fprintf(stderr, "jb: reinitialized existing jb repository in %s\n", cwd);
    } else {
        fprintf(stderr, "jb: initialized empty jb repository in %s\n", cwd);
    }
    return 0;
}

/* find_repo — walk up from start looking for .jb/. Returns 0 with the repo
   root in out, or -1 when no repo encloses start. */
static int find_repo(const char *start, char *out, size_t outlen)
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

/* Retry wrapper for api_chat — retries on transient errors */
static int api_chat_with_retry(const jb_config *cfg, const char *sys_prompt,
                               cJSON *messages, cJSON *tools, api_response *resp);

static int cmd_run(const char *config_path, const char *argv0, int run_argc, char **run_argv);
static int api_chat_with_retry(const jb_config *cfg, const char *sys_prompt,
                               cJSON *messages, cJSON *tools, api_response *resp)
{
    int max_retries = 3;
    int base_delay = 2;  /* seconds */

    for (int attempt = 0; attempt <= max_retries; attempt++) {
        int rc = api_chat(cfg, sys_prompt, messages, tools, resp);

        if (rc == 0) return 0;

        /* Log the error */
        if (resp->text) {
            /* Check for non-retryable errors (auth, bad request) */
            if (strstr(resp->text, "401") || strstr(resp->text, "Unauthorized") ||
                strstr(resp->text, "400") || strstr(resp->text, "invalid_api_key")) {
                return -1;
            }
        }

        if (attempt < max_retries) {
            int delay = base_delay << attempt;  /* exponential backoff */
            sleep((unsigned)delay);
        }
    }

    return -1;
}

/* ---- pi-format message builders (§3.3) ---- */

/* usage object — zeros when the provider sends nothing (jb has no pricing) */
static cJSON *usage_object(long prompt_tokens, long completion_tokens,
                           long reasoning_tokens, long total_tokens)
{
    cJSON *u = cJSON_CreateObject();
    cJSON_AddNumberToObject(u, "input", prompt_tokens);
    cJSON_AddNumberToObject(u, "output", completion_tokens);
    cJSON_AddNumberToObject(u, "cacheRead", 0);
    cJSON_AddNumberToObject(u, "cacheWrite", 0);
    cJSON_AddNumberToObject(u, "reasoning", reasoning_tokens);
    cJSON_AddNumberToObject(u, "totalTokens", total_tokens);
    cJSON *cost = cJSON_CreateObject();
    cJSON_AddNumberToObject(cost, "input", 0);
    cJSON_AddNumberToObject(cost, "output", 0);
    cJSON_AddNumberToObject(cost, "cacheRead", 0);
    cJSON_AddNumberToObject(cost, "cacheWrite", 0);
    cJSON_AddNumberToObject(cost, "total", 0);
    cJSON_AddItemToObject(u, "cost", cost);
    return u;
}

/* provider = hostname of api_url, reference §3.4:
   api.openai.com→openai, localhost:11434→ollama, else the host. */
static void provider_from_url(const char *api_url, char *out, size_t outlen)
{
    const char *p = api_url;
    const char *scheme = strstr(p, "://");
    if (scheme) p = scheme + 3;

    const char *end = p;
    while (*end && *end != '/' && *end != ':') end++;

    size_t len = (size_t)(end - p);
    if (len >= outlen) len = outlen - 1;
    memcpy(out, p, len);
    out[len] = '\0';

    /* localhost:11434 → ollama */
    if (strcmp(out, "localhost") == 0 && *end == ':') {
        const char *port = end + 1;
        if (strncmp(port, "11434", 5) == 0 && (port[5] == '\0' || port[5] == '/')) {
            snprintf(out, outlen, "ollama");
            return;
        }
    }
    /* api.openai.com → openai: drop the api. prefix and the .com suffix */
    if (strncmp(out, "api.", 4) == 0)
        memmove(out, out + 4, strlen(out + 4) + 1);
    size_t olen = strlen(out);
    if (olen > 4 && strcmp(out + olen - 4, ".com") == 0)
        out[olen - 4] = '\0';
}

/* Assistant message from a completed api_response. stop_reason ∈
   {toolUse, stop, error}; error_message only for error. */
static cJSON *assistant_message(const jb_config *cfg, const api_response *resp,
                                const char *stop_reason, const char *error_message)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "assistant");

    cJSON *content = cJSON_CreateArray();
    /* text block — omitted when empty; never for error replies */
    if (strcmp(stop_reason, "error") != 0 && resp->text && resp->text[0]) {
        cJSON *block = cJSON_CreateObject();
        cJSON_AddStringToObject(block, "type", "text");
        cJSON_AddStringToObject(block, "text", resp->text);
        cJSON_AddItemToArray(content, block);
    }
    /* toolCall blocks — parsed arguments OBJECT, not a string (§3.4) */
    if (resp->tool_calls_arr) {
        int n = cJSON_GetArraySize(resp->tool_calls_arr);
        for (int i = 0; i < n; i++) {
            cJSON *tc = cJSON_GetArrayItem(resp->tool_calls_arr, i);
            const char *id = cJSON_GetObjectItemCaseSensitive(tc, "id")->valuestring;
            const char *name = cJSON_GetObjectItemCaseSensitive(tc, "name")->valuestring;
            const char *args = cJSON_GetObjectItemCaseSensitive(tc, "arguments")->valuestring;
            cJSON *block = cJSON_CreateObject();
            cJSON_AddStringToObject(block, "type", "toolCall");
            cJSON_AddStringToObject(block, "id", id);
            cJSON_AddStringToObject(block, "name", name);
            cJSON *parsed = cJSON_Parse(args ? args : "");
            if (!parsed) parsed = cJSON_CreateObject();
            cJSON_AddItemToObject(block, "arguments", parsed);
            cJSON_AddItemToArray(content, block);
        }
    }
    cJSON_AddItemToObject(msg, "content", content);

    cJSON_AddStringToObject(msg, "api", "openai-completions");
    char provider[128];
    provider_from_url(cfg->api_url, provider, sizeof(provider));
    cJSON_AddStringToObject(msg, "provider", provider);
    cJSON_AddStringToObject(msg, "model", cfg->model);
    cJSON_AddItemToObject(msg, "usage",
        usage_object(resp->prompt_tokens, resp->completion_tokens,
                     resp->reasoning_tokens, resp->total_tokens));
    cJSON_AddStringToObject(msg, "stopReason", stop_reason);
    if (error_message && error_message[0])
        cJSON_AddStringToObject(msg, "errorMessage", error_message);
    cJSON_AddNumberToObject(msg, "timestamp", jb_epoch_ms());
    return msg;
}

/* toolResult message — one per executed tool */
static cJSON *tool_result_message(const char *call_id, const char *name,
                                  const char *result, int is_error)
{
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "toolResult");
    cJSON_AddStringToObject(msg, "toolCallId", call_id);
    cJSON_AddStringToObject(msg, "toolName", name);
    cJSON *content = cJSON_CreateArray();
    cJSON *block = cJSON_CreateObject();
    cJSON_AddStringToObject(block, "type", "text");
    cJSON_AddStringToObject(block, "text", result);
    cJSON_AddItemToArray(content, block);
    cJSON_AddItemToObject(msg, "content", content);
    cJSON_AddBoolToObject(msg, "isError", is_error);
    cJSON_AddNumberToObject(msg, "timestamp", jb_epoch_ms());
    return msg;
}

/* isError: bash non-zero exit ([exit code: N]) or result starts with "Error:" */
static int tool_result_is_error(const char *result)
{
    if (strncmp(result, "Error:", 6) == 0) return 1;
    if (strstr(result, "[exit code:") != NULL) return 1;
    return 0;
}

int main(int argc, char **argv)
{
    /* Global flags before the verb (git-style): -C DIR, --config PATH,
       --version, --help. Then dispatch on the first non-flag token. */
    const char *dir_arg = NULL;   /* -C DIR */
    const char *config_path = NULL;
    int config_count = 0;
    int i = 1;

    for (; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("jb %s\n", JB_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            cmd_help(NULL);
            return 0;
        }
        if (strcmp(argv[i], "-C") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "jb: option '-C' requires a value\n");
                return 2;
            }
            dir_arg = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--config") == 0) {
            config_count++;
            if (config_count > 1) {
                fprintf(stderr, "jb: --config specified multiple times\n");
                return 2;
            }
            if (i + 1 >= argc) {
                fprintf(stderr, "jb: --config requires a path\n");
                return 2;
            }
            config_path = argv[++i];
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "jb: unknown option '%s' (see 'jb help')\n", argv[i]);
            return 2;
        }
        break;  /* first non-flag token = the verb */
    }

    /* -C DIR: run as if started in DIR (git -C analog) */
    if (dir_arg) {
        if (chdir(dir_arg) != 0) {
            fprintf(stderr, "jb: fatal: cannot change to '%s'\n", dir_arg);
            return 1;
        }
    }

    /* Bare jb = jb help */
    if (i >= argc) {
        cmd_help(NULL);
        return 0;
    }

    const char *verb = argv[i];
    const char *verb_arg = (i + 1 < argc) ? argv[i + 1] : NULL;

    if (strcmp(verb, "help") == 0) {
        cmd_help(verb_arg);
        return 0;
    }
    if (strcmp(verb, "init") == 0) {
        return cmd_init();
    }
    if (strcmp(verb, "run") == 0) {
        return cmd_run(config_path, argv[0], argc - i - 1, argv + i + 1);
    }

    fprintf(stderr, "jb: unknown command '%s' (see 'jb help')\n", verb);
    return 2;
}

static int cmd_run(const char *config_path, const char *argv0, int run_argc, char **run_argv)
{
    /* run verb flags: --config PATH (may also appear as a global flag) */
    for (int j = 0; j < run_argc; j++) {
        if (strcmp(run_argv[j], "--config") == 0) {
            if (config_path) {
                fprintf(stderr, "jb: --config specified multiple times\n");
                return 2;
            }
            if (j + 1 >= run_argc) {
                fprintf(stderr, "jb: --config requires a path\n");
                return 2;
            }
            config_path = run_argv[++j];
        } else {
            fprintf(stderr, "jb: unknown option '%s' for 'run'\n", run_argv[j]);
            return 2;
        }
    }

    /* Resolve the repository: walk up from cwd (fatal outside any repo) */
    char cwd_buf[4096];
    const char *cwd = getcwd(cwd_buf, sizeof(cwd_buf)) ? cwd_buf : ".";
    char repo_root[4096];
    if (find_repo(cwd, repo_root, sizeof(repo_root)) != 0) {
        fprintf(stderr, "jb: fatal: not a jb repository (run 'jb init')\n");
        return 1;
    }

    jb_config cfg;

    /* Load config — failure is an error (exit 1) */
    if (config_load(&cfg, config_path) != 0) {
        return 1;
    }

    /* Pass resolved config path to tools for child jb inheritance */
    tools_set_config_path(config_get_resolved_path());

    /* Initialize session under the repo */
    if (session_init(&g_session, repo_root) != 0) {
        return 1;
    }
    g_session_active = 1;

    /* Author: the spawning session ($JB_SESSION) or "" for a human run */
    {
        const char *jb_env = getenv("JB_SESSION");
        if (jb_env && jb_env[0]) session_set_author(&g_session, jb_env);
    }

    /* Export our own identity so children inherit provenance */
    setenv("JB_SESSION", g_session.uuid, 1);

    /* Install signal handlers */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    jb_session *sess = &g_session;

    /* Stderr banner */
    fprintf(stderr, "jb: session %.8s started\n", sess->uuid);

    /* Read prompt from stdin */
    char *user_prompt = read_stdin();
    if (!user_prompt || !user_prompt[0]) {
        fprintf(stderr, "jb: no prompt on stdin\n");
        session_close(sess);
        free(user_prompt);
        return 2;
    }

    /* Resolve jb binary path for the jb tool */
    char jb_abs[4096];
    if (argv0[0] == '/') {
        strncpy(jb_abs, argv0, sizeof(jb_abs) - 1);
    } else {
        /* Resolve relative to cwd */
        snprintf(jb_abs, sizeof(jb_abs), "%s/%s", cwd, argv0);
    }
    /* Canonicalize (remove . and ..) */
    char jb_resolved[4096];
    if (realpath(jb_abs, jb_resolved)) {
        tools_set_jb_path(jb_resolved);
    } else {
        tools_set_jb_path(argv0);  /* fallback */
    }

    /* Write initial metadata (status: working, subject, config snapshot) */
    session_write_metadata_init(sess, user_prompt, cwd, &cfg);

    /* session.jsonl + events.jsonl: v3 header, then the user entry */
    session_write_header(sess, cwd);

    /* Build the initial messages array — pi format (§3.3). The wire body is
       derived in build_request_body(); the system prompt is prepended at
       request time and never persisted. */
    cJSON *messages = cJSON_CreateArray();

    /* User message: persisted via session_append_message (entry base: id,
       parentId, timestamps), then owned by the messages array. */
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON *ucontent = cJSON_CreateArray();
    cJSON *ublock = cJSON_CreateObject();
    cJSON_AddStringToObject(ublock, "type", "text");
    cJSON_AddStringToObject(ublock, "text", user_prompt);
    cJSON_AddItemToArray(ucontent, ublock);
    cJSON_AddItemToObject(user_msg, "content", ucontent);
    cJSON_AddNumberToObject(user_msg, "timestamp", jb_epoch_ms());
    session_append_message(sess, user_msg);
    cJSON_AddItemToArray(messages, user_msg);

    free(user_prompt);

    /* Build system prompt (kept for the whole run — prepended per request) */
    char *sys_prompt = prompt_build();

    /* Get tool definitions */
    cJSON *tools = tools_get_definitions();
    tools_set_limits(cfg.max_output_lines, cfg.max_output_bytes);
    tools_set_session(sess->uuid);

    /* Agentic loop */
    long total_tokens = 0;
    int max_turns = 50;  /* safety limit */
    int turn = 0;
    int exit_code = 0;

    while (turn++ < max_turns) {
        /* Check token budget */
        if (cfg.max_tokens > 0 && total_tokens >= cfg.max_tokens) {
            exit_code = 2;  /* budget exhausted */
            break;
        }

        api_response resp;
        api_response_init(&resp);

        int rc = api_chat_with_retry(&cfg, sys_prompt, messages, tools, &resp);

        if (rc != 0) {
            /* API error after retries — record it, stop */
            exit_code = 1;
            cJSON *err_msg = assistant_message(&cfg, &resp, "error",
                resp.text ? resp.text : "API error");
            session_append_message(sess, err_msg);
            cJSON_AddItemToArray(messages, err_msg);
            free(resp.text);
            break;
        }

        /* Accumulate tokens */
        total_tokens += resp.total_tokens;

        if (resp.finish_tool_calls && resp.tool_calls_arr) {
            /* Model wants to call tools — pi-format assistant message with
               toolCall blocks, persisted like every other entry */
            cJSON *assistant_msg = assistant_message(&cfg, &resp, "toolUse", NULL);
            session_append_message(sess, assistant_msg);
            cJSON_AddItemToArray(messages, assistant_msg);

            if (resp.text && resp.text[0]) {
                /* Track partial answer for signal handler */
                free(g_partial_answer);
                g_partial_answer = strdup(resp.text);
            }

            /* Execute each tool; each result becomes a toolResult entry */
            int n_tc = cJSON_GetArraySize(resp.tool_calls_arr);
            for (int i = 0; i < n_tc; i++) {
                cJSON *tc = cJSON_GetArrayItem(resp.tool_calls_arr, i);
                const char *id = cJSON_GetObjectItemCaseSensitive(tc, "id")->valuestring;
                const char *name = cJSON_GetObjectItemCaseSensitive(tc, "name")->valuestring;
                const char *args = cJSON_GetObjectItemCaseSensitive(tc, "arguments")->valuestring;

                char *result = tool_execute(name, args);

                cJSON *tool_msg = tool_result_message(id, name, result,
                                                      tool_result_is_error(result));
                session_append_message(sess, tool_msg);
                cJSON_AddItemToArray(messages, tool_msg);
                free(result);
            }

            cJSON_Delete(resp.tool_calls_arr);
            free(resp.text);
            continue;
        }

        /* finish_reason == "stop" — print final answer */
        if (resp.text) {
            printf("%s\n", resp.text);
            fflush(stdout);
            free(g_partial_answer);
            g_partial_answer = strdup(resp.text);
        }

        /* Persist the assistant entry — session.jsonl is the record */
        cJSON *final_msg = assistant_message(&cfg, &resp, "stop", NULL);
        session_append_message(sess, final_msg);
        cJSON_AddItemToArray(messages, final_msg);
        free(resp.text);
        break;
    }

    free(sys_prompt);

    /* Write final metadata (status: completed|error) */
    {
        const char *status = (exit_code == 0) ? "completed" : "error";
        session_write_metadata_close(sess, status, total_tokens, turn, exit_code);
    }

    cJSON_Delete(messages);
    cJSON_Delete(tools);
    session_close(sess);
    g_session_active = 0;
    free(g_partial_answer);

    return exit_code;
}
