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
    long reasoning_tokens;
    long total_tokens;

    /* Done flag */
    int done;

    /* Delta callback (§5) — fires per text/toolcall delta */
    sse_delta_cb delta_cb;
    void *delta_userdata;
} sse_state;

/* Points at the active sse_state's text while a stream is in flight — the
   SIGINT handler snapshots it. NULL between api_chat calls (the buffer is
   freed), so it is only valid while the main thread is inside the read
   loop. */
static const char *g_stream_text = NULL;

static void sse_state_init(sse_state *st)
{
    memset(st, 0, sizeof(*st));
    st->text_cap = 4096;
    st->text = malloc(st->text_cap);
    st->text[0] = '\0';
    st->text_len = 0;
    st->tc_array = cJSON_CreateArray();
}

/* Fire a delta event when a callback is attached. */
static void delta_fire(sse_state *st, const sse_delta *d)
{
    if (st->delta_cb) st->delta_cb(d, st->delta_userdata);
}

/* Fire the text delta for a content chunk (block 0). */
static void delta_fire_text(sse_state *st, const char *s)
{
    if (!s || !s[0]) return;
    sse_delta d;
    memset(&d, 0, sizeof(d));
    d.kind = SSE_DELTA_TEXT;
    d.content_index = 0;
    d.text = s;
    delta_fire(st, &d);
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
    /* Keep the snapshot pointer valid and NUL-terminated at every
       instruction boundary: assigned only after realloc, and the terminator
       is written BEFORE the copy, so a signal landing mid-append can never
       read a freed buffer or run past the end. */
    g_stream_text = st->text;
    st->text[st->text_len + slen] = '\0';
    memcpy(st->text + st->text_len, s, slen);
    st->text_len += slen;
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
    /* Content index (§5): toolCall blocks come after the text block, in
       emission order — 0 when no text has been streamed. Computed at
       creation; providers stream text before tool calls in practice. */
    cJSON_AddNumberToObject(item, "_cindex", (st->text_len > 0 ? 1 : 0) + index);
    cJSON_AddStringToObject(item, "id", "");
    cJSON_AddStringToObject(item, "name", "");
    cJSON_AddStringToObject(item, "arguments", "");
    cJSON_AddItemToArray(st->tc_array, item);
    return item;
}

/* Fire toolcall_start on creation (id/name as accumulated so far). */
static void delta_fire_toolcall_start(sse_state *st, cJSON *tc)
{
    sse_delta d;
    memset(&d, 0, sizeof(d));
    d.kind = SSE_DELTA_TOOLCALL_START;
    d.content_index = cJSON_GetObjectItemCaseSensitive(tc, "_cindex")->valueint;
    d.id = cJSON_GetObjectItemCaseSensitive(tc, "id")->valuestring;
    d.name = cJSON_GetObjectItemCaseSensitive(tc, "name")->valuestring;
    d.args = "";
    delta_fire(st, &d);
}

/* Fire toolcall_delta for an arguments fragment. */
static void delta_fire_toolcall_delta(sse_state *st, cJSON *tc, const char *args)
{
    if (!args || !args[0]) return;
    sse_delta d;
    memset(&d, 0, sizeof(d));
    d.kind = SSE_DELTA_TOOLCALL_DELTA;
    d.content_index = cJSON_GetObjectItemCaseSensitive(tc, "_cindex")->valueint;
    d.id = cJSON_GetObjectItemCaseSensitive(tc, "id")->valuestring;
    d.name = cJSON_GetObjectItemCaseSensitive(tc, "name")->valuestring;
    d.text = args;
    d.args = args;
    delta_fire(st, &d);
}

/* Fire toolcall_end for an accumulator at stream end. */
static void delta_fire_toolcall_end(sse_state *st, cJSON *tc)
{
    sse_delta d;
    memset(&d, 0, sizeof(d));
    d.kind = SSE_DELTA_TOOLCALL_END;
    d.content_index = cJSON_GetObjectItemCaseSensitive(tc, "_cindex")->valueint;
    d.id = cJSON_GetObjectItemCaseSensitive(tc, "id")->valuestring;
    d.name = cJSON_GetObjectItemCaseSensitive(tc, "name")->valuestring;
    d.args = cJSON_GetObjectItemCaseSensitive(tc, "arguments")->valuestring;
    delta_fire(st, &d);
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
                delta_fire_text(st, content->valuestring);
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
                    int fresh = cJSON_GetObjectItemCaseSensitive(tc, "_started") == NULL;
                    if (fresh)
                        cJSON_AddTrueToObject(tc, "_started");

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
                    if (fresh) delta_fire_toolcall_start(st, tc);
                    if (args && args[0]) delta_fire_toolcall_delta(st, tc, args);
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
        /* completion_tokens_details.reasoning_tokens (0 if absent) */
        cJSON *ctd = cJSON_GetObjectItemCaseSensitive(usage, "completion_tokens_details");
        if (ctd) {
            cJSON *rt = cJSON_GetObjectItemCaseSensitive(ctd, "reasoning_tokens");
            if (rt && cJSON_IsNumber(rt)) st->reasoning_tokens = (long)rt->valuedouble;
        }
    }

    cJSON_Delete(root);
}

/* ---- Wire conversion (§4): pi-format messages → OpenAI wire ----
   The in-memory message array IS the pi format (§3.3). The wire body is
   built from it here — nowhere else. The system prompt is prepended at
   request time and never persisted. */

/* Join the text blocks of a pi message into one string ("" if none). */
static char *pi_joined_text(const cJSON *msg)
{
    const cJSON *content = cJSON_GetObjectItemCaseSensitive(msg, "content");
    if (!content || !cJSON_IsArray(content) || cJSON_GetArraySize(content) == 0)
        return strdup("");

    size_t len = 0, cap = 0;
    char *buf = NULL;
    for (int i = 0; i < cJSON_GetArraySize(content); i++) {
        const cJSON *b = cJSON_GetArrayItem(content, i);
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(b, "text");
        if (!t || !cJSON_IsString(t)) continue;
        size_t sl = strlen(t->valuestring);
        if (len + sl + 1 > cap) {
            cap = cap ? cap * 2 : 256;
            while (cap < len + sl + 1) cap *= 2;
            buf = realloc(buf, cap);
        }
        memcpy(buf + len, t->valuestring, sl);
        len += sl;
    }
    if (!buf) return strdup("");
    buf[len] = '\0';
    return buf;
}

/* toolCall blocks of a pi assistant message → wire tool_calls array. */
static cJSON *pi_toolcalls_to_wire(const cJSON *msg)
{
    const cJSON *content = cJSON_GetObjectItemCaseSensitive(msg, "content");
    if (!content || !cJSON_IsArray(content)) return NULL;

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < cJSON_GetArraySize(content); i++) {
        const cJSON *b = cJSON_GetArrayItem(content, i);
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(b, "type");
        if (!type || !cJSON_IsString(type) || strcmp(type->valuestring, "toolCall") != 0)
            continue;

        const cJSON *id = cJSON_GetObjectItemCaseSensitive(b, "id");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(b, "name");
        const cJSON *args = cJSON_GetObjectItemCaseSensitive(b, "arguments");
        if (!id || !cJSON_IsString(id) || !name || !cJSON_IsString(name)) continue;

        cJSON *tc = cJSON_CreateObject();
        cJSON_AddStringToObject(tc, "id", id->valuestring);
        cJSON_AddStringToObject(tc, "type", "function");
        cJSON *fn = cJSON_CreateObject();
        cJSON_AddStringToObject(fn, "name", name->valuestring);
        if (args && cJSON_IsObject(args)) {
            char *a = cJSON_PrintUnformatted(args);
            cJSON_AddStringToObject(fn, "arguments", a ? a : "{}");
            free(a);
        } else {
            cJSON_AddStringToObject(fn, "arguments", "{}");
        }
        cJSON_AddItemToObject(tc, "function", fn);
        cJSON_AddItemToArray(arr, tc);
    }
    if (cJSON_GetArraySize(arr) == 0) {
        cJSON_Delete(arr);
        return NULL;
    }
    return arr;
}

/* Build the curl command and pipe its stdout */
static char *build_request_body(const jb_config *cfg, const char *sys_prompt,
                                cJSON *messages, cJSON *tools)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", cfg->model);

    cJSON *wire = cJSON_CreateArray();

    /* System prompt: prepended at request time — never persisted */
    cJSON *sys = cJSON_CreateObject();
    cJSON_AddStringToObject(sys, "role", "system");
    cJSON_AddStringToObject(sys, "content", sys_prompt ? sys_prompt : "");
    cJSON_AddItemToArray(wire, sys);

    for (const cJSON *m = messages->child; m; m = m->next) {
        const cJSON *role = cJSON_GetObjectItemCaseSensitive(m, "role");
        if (!role || !cJSON_IsString(role)) continue;
        const char *r = role->valuestring;

        cJSON *wm = cJSON_CreateObject();
        if (strcmp(r, "user") == 0) {
            cJSON_AddStringToObject(wm, "role", "user");
            char *text = pi_joined_text(m);
            cJSON_AddStringToObject(wm, "content", text ? text : "");
            free(text);
        } else if (strcmp(r, "assistant") == 0) {
            cJSON_AddStringToObject(wm, "role", "assistant");
            char *text = pi_joined_text(m);
            if (text && text[0]) {
                cJSON_AddStringToObject(wm, "content", text);
            } else {
                cJSON_AddNullToObject(wm, "content");
            }
            free(text);
            cJSON *tcs = pi_toolcalls_to_wire(m);
            if (tcs) cJSON_AddItemToObject(wm, "tool_calls", tcs);
        } else if (strcmp(r, "toolResult") == 0) {
            cJSON_AddStringToObject(wm, "role", "tool");
            const cJSON *tcid = cJSON_GetObjectItemCaseSensitive(m, "toolCallId");
            cJSON_AddStringToObject(wm, "tool_call_id",
                tcid && cJSON_IsString(tcid) ? tcid->valuestring : "");
            char *text = pi_joined_text(m);
            cJSON_AddStringToObject(wm, "content", text ? text : "");
            free(text);
        }
        cJSON_AddItemToArray(wire, wm);
    }

    cJSON_AddItemToObject(root, "messages", wire);
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

int api_chat(const jb_config *cfg, const char *sys_prompt, cJSON *messages,
             cJSON *tools, api_response *resp, sse_delta_cb on_delta,
             void *delta_userdata)
{
    char *body = build_request_body(cfg, sys_prompt, messages, tools);
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
    st.delta_cb = on_delta;
    st.delta_userdata = delta_userdata;

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
                    g_stream_text = NULL;
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
        }
    }

    int curl_status = pclose(fp);
    remove(tmpfile);

    /* Stream over: fire toolcall_end for every accumulated tool call (§5) */
    for (int i = 0; i < cJSON_GetArraySize(st.tc_array); i++)
        delta_fire_toolcall_end(&st, cJSON_GetArrayItem(st.tc_array, i));

    /* Fill response */
    resp->text = st.text;
    st.text = NULL;  /* transfer ownership */
    resp->total_tokens = st.total_tokens;
    resp->prompt_tokens = st.prompt_tokens;
    resp->completion_tokens = st.completion_tokens;
    resp->reasoning_tokens = st.reasoning_tokens;

    if (strcmp(st.finish_reason, "tool_calls") == 0) {
        resp->finish_tool_calls = 1;
        /* Convert accumulated tool calls to proper array */
        resp->tool_calls_arr = cJSON_Duplicate(st.tc_array, 1);
    }

    g_stream_text = NULL;  /* the buffer is about to be freed */

    if (curl_status != 0 && !st.done && st.text_len == 0) {
        sse_state_free(&st);
        return -1;
    }

    sse_state_free(&st);
    return 0;
}

void api_stream_text_snapshot(char *out, size_t outlen)
{
    if (!g_stream_text || !g_stream_text[0]) {
        out[0] = '\0';
        return;
    }
    strncpy(out, g_stream_text, outlen - 1);
    out[outlen - 1] = '\0';
}
