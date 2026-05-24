/* api.c — HTTP/SSE communication with Chat Completions API via curl */
#include "api.h"
#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

/* ---- SSE line parser state ---- */

typedef struct {
    /* Accumulated text content */
    char *text;
    size_t text_len;
    size_t text_cap;

    /* Accumulated tool calls — keyed by index */
    cJSON *tc_array;    /* array of objects: {index, id, name, arguments} */

    /* Finish reason */
    char finish_reason[32];

    /* Usage */
    long prompt_tokens;
    long completion_tokens;
    long total_tokens;

    /* Done flag */
    int done;
} sse_state;

static void sse_state_init(sse_state *st)
{
    memset(st, 0, sizeof(*st));
    st->text_cap = 4096;
    st->text = malloc(st->text_cap);
    st->text[0] = '\0';
    st->text_len = 0;
    st->tc_array = cJSON_CreateArray();
}

static void sse_state_free(sse_state *st)
{
    free(st->text);
    cJSON_Delete(st->tc_array);
}

static void text_append(sse_state *st, const char *s)
{
    size_t slen = strlen(s);
    while (st->text_len + slen + 1 > st->text_cap) {
        st->text_cap *= 2;
        st->text = realloc(st->text, st->text_cap);
    }
    memcpy(st->text + st->text_len, s, slen);
    st->text_len += slen;
    st->text[st->text_len] = '\0';
}

/* Find or create a tool call accumulator by index */
static cJSON *tc_get_or_create(sse_state *st, int index)
{
    for (int i = 0; i < cJSON_GetArraySize(st->tc_array); i++) {
        cJSON *item = cJSON_GetArrayItem(st->tc_array, i);
        cJSON *idx = cJSON_GetObjectItemCaseSensitive(item, "_index");
        if (idx && idx->valueint == index) return item;
    }
    cJSON *item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "_index", index);
    cJSON_AddStringToObject(item, "id", "");
    cJSON_AddStringToObject(item, "name", "");
    cJSON_AddStringToObject(item, "arguments", "");
    cJSON_AddItemToArray(st->tc_array, item);
    return item;
}

static void tc_accumulate(cJSON *tc, const char *id, const char *name, const char *args)
{
    if (id && id[0]) {
        const char *old = cJSON_GetObjectItemCaseSensitive(tc, "id")->valuestring;
        char *newid = malloc(strlen(old) + strlen(id) + 1);
        strcpy(newid, old);
        strcat(newid, id);
        cJSON_ReplaceItemInObjectCaseSensitive(tc, "id", cJSON_CreateString(newid));
        free(newid);
    }
    if (name && name[0]) {
        const char *old = cJSON_GetObjectItemCaseSensitive(tc, "name")->valuestring;
        char *newname = malloc(strlen(old) + strlen(name) + 1);
        strcpy(newname, old);
        strcat(newname, name);
        cJSON_ReplaceItemInObjectCaseSensitive(tc, "name", cJSON_CreateString(newname));
        free(newname);
    }
    if (args && args[0]) {
        const char *old = cJSON_GetObjectItemCaseSensitive(tc, "arguments")->valuestring;
        char *newargs = malloc(strlen(old) + strlen(args) + 1);
        strcpy(newargs, old);
        strcat(newargs, args);
        cJSON_ReplaceItemInObjectCaseSensitive(tc, "arguments", cJSON_CreateString(newargs));
        free(newargs);
    }
}

/* Process one SSE data line */
static void process_sse_data(sse_state *st, const char *data)
{
    cJSON *root = cJSON_Parse(data);
    if (!root) return;

    cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    if (choices && cJSON_GetArraySize(choices) > 0) {
        cJSON *choice = cJSON_GetArrayItem(choices, 0);

        /* Content delta */
        cJSON *delta = cJSON_GetObjectItemCaseSensitive(choice, "delta");
        if (delta) {
            cJSON *content = cJSON_GetObjectItemCaseSensitive(delta, "content");
            if (content && cJSON_IsString(content) && content->valuestring) {
                text_append(st, content->valuestring);
            }

            /* Tool call deltas */
            cJSON *tc_arr = cJSON_GetObjectItemCaseSensitive(delta, "tool_calls");
            if (tc_arr) {
                for (int i = 0; i < cJSON_GetArraySize(tc_arr); i++) {
                    cJSON *tc_delta = cJSON_GetArrayItem(tc_arr, i);
                    int idx = 0;
                    cJSON *idx_j = cJSON_GetObjectItemCaseSensitive(tc_delta, "index");
                    if (idx_j && cJSON_IsNumber(idx_j)) idx = idx_j->valueint;

                    cJSON *tc = tc_get_or_create(st, idx);

                    const char *id = NULL;
                    cJSON *id_j = cJSON_GetObjectItemCaseSensitive(tc_delta, "id");
                    if (id_j && cJSON_IsString(id_j)) id = id_j->valuestring;

                    const char *name = NULL;
                    cJSON *fn = cJSON_GetObjectItemCaseSensitive(tc_delta, "function");
                    if (fn) {
                        cJSON *n_j = cJSON_GetObjectItemCaseSensitive(fn, "name");
                        if (n_j && cJSON_IsString(n_j)) name = n_j->valuestring;
                    }

                    const char *args = NULL;
                    if (fn) {
                        cJSON *a_j = cJSON_GetObjectItemCaseSensitive(fn, "arguments");
                        if (a_j && cJSON_IsString(a_j)) args = a_j->valuestring;
                    }

                    tc_accumulate(tc, id, name, args);
                }
            }
        }

        /* Finish reason */
        cJSON *fr = cJSON_GetObjectItemCaseSensitive(choice, "finish_reason");
        if (fr && cJSON_IsString(fr) && fr->valuestring) {
            strncpy(st->finish_reason, fr->valuestring, sizeof(st->finish_reason) - 1);
        }
    }

    /* Usage */
    cJSON *usage = cJSON_GetObjectItemCaseSensitive(root, "usage");
    if (usage) {
        cJSON *pt = cJSON_GetObjectItemCaseSensitive(usage, "prompt_tokens");
        if (pt && cJSON_IsNumber(pt)) st->prompt_tokens = (long)pt->valuedouble;
        cJSON *ct = cJSON_GetObjectItemCaseSensitive(usage, "completion_tokens");
        if (ct && cJSON_IsNumber(ct)) st->completion_tokens = (long)ct->valuedouble;
        cJSON *tt = cJSON_GetObjectItemCaseSensitive(usage, "total_tokens");
        if (tt && cJSON_IsNumber(tt)) st->total_tokens = (long)tt->valuedouble;
    }

    cJSON_Delete(root);
}

/* Build the curl command and pipe its stdout */
static char *build_request_body(const jb_config *cfg, cJSON *messages, cJSON *tools)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", cfg->model);
    cJSON_AddItemReferenceToObject(root, "messages", messages);
    cJSON_AddItemReferenceToObject(root, "tools", tools);
    cJSON_AddBoolToObject(root, "stream", 1);

    /* stream_options */
    cJSON *so = cJSON_CreateObject();
    cJSON_AddBoolToObject(so, "include_usage", 1);
    cJSON_AddItemToObject(root, "stream_options", so);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}

void api_response_init(api_response *resp)
{
    memset(resp, 0, sizeof(*resp));
}

void api_response_free(api_response *resp)
{
    free(resp->text);
    resp->text = NULL;
    /* tool_calls_arr is owned by caller or not allocated */
}

int api_chat(const jb_config *cfg, cJSON *messages, cJSON *tools, api_response *resp, jb_session *sess)
{
    char *body = build_request_body(cfg, messages, tools);
    if (!body) return -1;

    /* Build the URL: api_url + "/chat/completions" */
    char url[2048];
    snprintf(url, sizeof(url), "%s/chat/completions", cfg->api_url);

    /* Get API key */
    const char *key = getenv("JB_API_KEY");
    if (!key || !key[0]) key = getenv("OPENAI_API_KEY");

    /* Build curl command */
    /* Write body to a temp file to avoid shell escaping issues */
    char tmpfile[256];
    snprintf(tmpfile, sizeof(tmpfile), "/tmp/jb-body-%ld.tmp", (long)getpid());
    FILE *tf = fopen(tmpfile, "w");
    if (!tf) { free(body); return -1; }
    fputs(body, tf);
    fclose(tf);
    free(body);

    char cmd[8192];
    snprintf(cmd, sizeof(cmd),
        "curl -sS -X POST '%s' "
        "-H 'Content-Type: application/json' "
        "-H 'Authorization: Bearer %s' "
        "-d @%s",
        url, key, tmpfile);

    FILE *fp = popen(cmd, "r");
    if (!fp) { remove(tmpfile); return -1; }

    sse_state st;
    sse_state_init(&st);

    /* Read SSE lines */
    char line[65536];
    int first_line = 1;
    while (fgets(line, sizeof(line), fp)) {
        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* Check for non-SSE error response (JSON error from API) */
        if (first_line && line[0] == '{') {
            cJSON *err = cJSON_Parse(line);
            if (err) {
                cJSON *err_obj = cJSON_GetObjectItemCaseSensitive(err, "error");
                if (err_obj) {
                    /* This is a non-streaming API error */
                    char *err_msg = cJSON_PrintUnformatted(err_obj);
                    resp->text = err_msg;
                    cJSON_Delete(err);
                    pclose(fp);
                    remove(tmpfile);
                    return -1;
                }
                cJSON_Delete(err);
            }
        }
        first_line = 0;

        /* SSE format: data: ... */
        if (strncmp(line, "data: ", 6) == 0) {
            const char *data = line + 6;

            /* Check for [DONE] */
            if (strcmp(data, "[DONE]") == 0) {
                st.done = 1;
                continue;
            }

            process_sse_data(&st, data);

            /* Log SSE event to session log */
            if (sess) {
                session_append_log(sess, line);
            }
        }
    }

    int curl_status = pclose(fp);
    remove(tmpfile);

    /* Fill response */
    resp->text = st.text;
    st.text = NULL;  /* transfer ownership */
    resp->total_tokens = st.total_tokens;

    if (strcmp(st.finish_reason, "tool_calls") == 0) {
        resp->finish_tool_calls = 1;
        /* Convert accumulated tool calls to proper array */
        resp->tool_calls_arr = cJSON_Duplicate(st.tc_array, 1);
    }

    if (curl_status != 0 && !st.done && st.text_len == 0) {
        sse_state_free(&st);
        return -1;
    }

    sse_state_free(&st);
    return 0;
}
