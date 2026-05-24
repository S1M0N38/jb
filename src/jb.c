/* jb.c — main entry point and agentic loop */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "cJSON.h"
#include "config.h"
#include "session.h"
#include "api.h"
#include "tools.h"
#include "prompt.h"

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

/* Retry wrapper for api_chat — retries on transient errors */
static int api_chat_with_retry(const jb_config *cfg, cJSON *messages, cJSON *tools,
                               api_response *resp, jb_session *sess)
{
    int max_retries = 3;
    int base_delay = 2;  /* seconds */

    for (int attempt = 0; attempt <= max_retries; attempt++) {
        int rc = api_chat(cfg, messages, tools, resp);

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

            /* Sleep */
            sleep((unsigned)delay);
        }
    }

    return -1;
}

int main(void)
{
    jb_config cfg;
    jb_session sess;

    /* Load config — exits with code 3 on failure */
    if (config_load(&cfg) != 0) {
        return 3;
    }

    /* Initialize session */
    if (session_init(&sess) != 0) {
        return 3;
    }

    /* Read prompt from stdin */
    char *user_prompt = read_stdin();
    if (!user_prompt || !user_prompt[0]) {
        fprintf(stderr, "jb: no prompt on stdin\n");
        session_close(&sess);
        free(user_prompt);
        return 3;
    }

    /* Build system prompt */
    char *sys_prompt = prompt_build();

    /* Build initial messages array */
    cJSON *messages = cJSON_CreateArray();

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

    /* Append initial messages to state */
    {
        cJSON *state_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(state_msg, "role", "system");
        cJSON_AddStringToObject(state_msg, "content", "...");
        char *s = cJSON_PrintUnformatted(state_msg);
        session_append_state(&sess, s);
        free(s);
        cJSON_Delete(state_msg);
    }
    {
        cJSON *state_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(state_msg, "role", "user");
        cJSON_AddStringToObject(state_msg, "content", user_prompt);
        char *s = cJSON_PrintUnformatted(state_msg);
        session_append_state(&sess, s);
        free(s);
        cJSON_Delete(state_msg);
    }

    free(user_prompt);

    /* Get tool definitions */
    cJSON *tools = tools_get_definitions();
    tools_set_limits(cfg.max_output_lines, cfg.max_output_bytes);

    /* Agentic loop */
    long total_tokens = 0;
    int max_turns = 50;  /* safety limit */
    int turn = 0;

    while (turn++ < max_turns) {
        /* Check token budget */
        if (cfg.max_tokens > 0 && total_tokens >= cfg.max_tokens) {
            break;
        }

        api_response resp;
        api_response_init(&resp);

        int rc = api_chat_with_retry(&cfg, messages, tools, &resp, &sess);

        if (rc != 0) {
            /* API error after retries */
            cJSON_Delete(messages);
            cJSON_Delete(tools);
            session_close(&sess);
            return 1;
        }

        /* Accumulate tokens */
        total_tokens += resp.total_tokens;

        if (resp.finish_tool_calls && resp.tool_calls_arr) {
            /* Model wants to call tools */
            cJSON *assistant_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(assistant_msg, "role", "assistant");

            if (resp.text && resp.text[0]) {
                cJSON_AddStringToObject(assistant_msg, "content", resp.text);
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
                session_append_state(&sess, s);
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
                    session_append_state(&sess, s);
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
            printf("%s", resp.text);
            fflush(stdout);
        }
        free(resp.text);
        break;
    }

    cJSON_Delete(messages);
    cJSON_Delete(tools);
    session_close(&sess);
    return 0;
}
