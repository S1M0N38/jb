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

static void handle_signal(int sig)
{
    if (g_session_active) {
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

/* Retry wrapper for api_chat — retries on transient errors */
static int api_chat_with_retry(const jb_config *cfg, cJSON *messages, cJSON *tools,
                               api_response *resp, jb_session *sess);

static int cmd_run(const char *config_path, const char *argv0);
static int api_chat_with_retry(const jb_config *cfg, cJSON *messages, cJSON *tools,
                               api_response *resp, jb_session *sess)
{
    int max_retries = 3;
    int base_delay = 2;  /* seconds */

    for (int attempt = 0; attempt <= max_retries; attempt++) {
        int rc = api_chat(cfg, messages, tools, resp, sess);

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
            char msg[128];
            snprintf(msg, sizeof(msg), "{\"event\":\"retry\",\"attempt\":%d,\"delay\":%d}", attempt + 1, delay);
            session_append_log(sess, msg);

            sleep((unsigned)delay);
        }
    }

    return -1;
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
        return cmd_run(config_path, argv[0]);
    }

    fprintf(stderr, "jb: unknown command '%s' (see 'jb help')\n", verb);
    return 2;
}

static int cmd_run(const char *config_path, const char *argv0)
{
    jb_config cfg;

    /* Load config — failure is an error (exit 1) */
    if (config_load(&cfg, config_path) != 0) {
        return 1;
    }

    /* Pass resolved config path to tools for child jb inheritance */
    tools_set_config_path(config_get_resolved_path());

    /* Initialize session */
    if (session_init(&g_session) != 0) {
        return 1;
    }
    g_session_active = 1;

    /* Export our own identity so children inherit provenance */
    setenv("JB_SESSION", g_session.uuid, 1);

    /* Install signal handlers */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    jb_session *sess = &g_session;

    /* Read prompt from stdin */
    char *user_prompt = read_stdin();
    if (!user_prompt || !user_prompt[0]) {
        fprintf(stderr, "jb: no prompt on stdin\n");
        session_close(sess);
        free(user_prompt);
        return 2;
    }

    /* Get working directory */
    char cwd_buf[4096];
    const char *cwd = getcwd(cwd_buf, sizeof(cwd_buf)) ? cwd_buf : ".";

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

    /* Write initial metadata (status: running) — derives title from prompt */
    session_write_metadata_init(sess, user_prompt, cwd, &cfg);

    /* Build initial messages array */
    cJSON *messages = cJSON_CreateArray();

    /* Build system prompt */
    char *sys_prompt = prompt_build();

    /* System message */
    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", sys_prompt);
    cJSON_AddItemToArray(messages, sys_msg);
    free(sys_prompt);

    /* User message */
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", user_prompt);
    cJSON_AddItemToArray(messages, user_msg);

    /* Append user message to state */
    {
        char *s = cJSON_PrintUnformatted(user_msg);
        session_append_state(sess, s);
        free(s);
    }

    free(user_prompt);

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

        int rc = api_chat_with_retry(&cfg, messages, tools, &resp, sess);

        if (rc != 0) {
            /* API error after retries */
            exit_code = 1;
            break;
        }

        /* Accumulate tokens */
        total_tokens += resp.total_tokens;

        if (resp.finish_tool_calls && resp.tool_calls_arr) {
            /* Model wants to call tools */
            cJSON *assistant_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(assistant_msg, "role", "assistant");

            if (resp.text && resp.text[0]) {
                cJSON_AddStringToObject(assistant_msg, "content", resp.text);
                /* Track partial answer for signal handler */
                free(g_partial_answer);
                g_partial_answer = strdup(resp.text);
            } else {
                cJSON_AddNullToObject(assistant_msg, "content");
            }

            /* Build tool_calls array in API format */
            cJSON *tc_api = cJSON_CreateArray();
            int n_tc = cJSON_GetArraySize(resp.tool_calls_arr);
            for (int i = 0; i < n_tc; i++) {
                cJSON *tc = cJSON_GetArrayItem(resp.tool_calls_arr, i);
                cJSON *tc_obj = cJSON_CreateObject();
                const char *id = cJSON_GetObjectItemCaseSensitive(tc, "id")->valuestring;
                const char *name = cJSON_GetObjectItemCaseSensitive(tc, "name")->valuestring;
                const char *args = cJSON_GetObjectItemCaseSensitive(tc, "arguments")->valuestring;
                cJSON_AddStringToObject(tc_obj, "id", id);
                cJSON_AddStringToObject(tc_obj, "type", "function");
                cJSON *fn = cJSON_CreateObject();
                cJSON_AddStringToObject(fn, "name", name);
                cJSON_AddStringToObject(fn, "arguments", args);
                cJSON_AddItemToObject(tc_obj, "function", fn);
                cJSON_AddItemToArray(tc_api, tc_obj);
            }
            cJSON_AddItemToObject(assistant_msg, "tool_calls", tc_api);
            cJSON_AddItemToArray(messages, assistant_msg);

            /* Append to state */
            {
                char *s = cJSON_PrintUnformatted(assistant_msg);
                session_append_state(sess, s);
                free(s);
            }

            /* Execute each tool and add results */
            for (int i = 0; i < n_tc; i++) {
                cJSON *tc = cJSON_GetArrayItem(resp.tool_calls_arr, i);
                const char *id = cJSON_GetObjectItemCaseSensitive(tc, "id")->valuestring;
                const char *name = cJSON_GetObjectItemCaseSensitive(tc, "name")->valuestring;
                const char *args = cJSON_GetObjectItemCaseSensitive(tc, "arguments")->valuestring;

                char *result = tool_execute(name, args);

                cJSON *tool_msg = cJSON_CreateObject();
                cJSON_AddStringToObject(tool_msg, "role", "tool");
                cJSON_AddStringToObject(tool_msg, "tool_call_id", id);
                cJSON_AddStringToObject(tool_msg, "content", result);
                cJSON_AddItemToArray(messages, tool_msg);

                {
                    char *s = cJSON_PrintUnformatted(tool_msg);
                    session_append_state(sess, s);
                    free(s);
                }

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

            /* Persist final assistant message to state */
            cJSON *final_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(final_msg, "role", "assistant");
            cJSON_AddStringToObject(final_msg, "content", resp.text);
            cJSON_AddItemToArray(messages, final_msg);
            {
                char *s = cJSON_PrintUnformatted(final_msg);
                session_append_state(sess, s);
                free(s);
            }
        }
        free(resp.text);
        break;
    }

    /* Log token usage */
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "{\"event\":\"done\",\"tokens\":%ld,\"turns\":%d}", total_tokens, turn);
        session_append_log(sess, msg);
    }

    /* Write final metadata (overwrite with completed/error status) */
    {
        const char *status;
        switch (exit_code) {
            case 0:   status = "completed"; break;
            case 1:   status = "error"; break;
            case 2:   status = "budget_exhausted"; break;
            case 130: status = "interrupted"; break;
            case 143: status = "terminated"; break;
            default:  status = "unknown"; break;
        }
        session_write_metadata_close(sess, status, total_tokens, turn, exit_code);
    }

    cJSON_Delete(messages);
    cJSON_Delete(tools);
    session_close(sess);
    g_session_active = 0;
    free(g_partial_answer);

    return exit_code;
}
