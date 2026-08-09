/* api.h — HTTP/SSE communication with Chat Completions API */
#ifndef JB_API_H
#define JB_API_H

#include "config.h"
#include "cJSON.h"
#include "session.h"
#include <sys/types.h>

/* Parsed state from a completed SSE stream */
typedef struct {
    char *text;             /* accumulated text content (final answer) */
    char *thinking;         /* accumulated reasoning_content deltas ("" if none) */
    int finish_tool_calls;  /* 1 if finish_reason was "tool_calls" */
    cJSON *tool_calls_arr;  /* array of accumulated tool call objects */
    long total_tokens;      /* from usage field */
    long prompt_tokens;     /* usage: input tokens */
    long completion_tokens; /* usage: output tokens */
    long reasoning_tokens;  /* usage: reasoning tokens (0 if absent) */
} api_response;

/* ---- Streaming delta callback (§5) ---- */

typedef enum {
    SSE_DELTA_TEXT = 0,          /* text_delta */
    SSE_DELTA_THINKING,          /* thinking_delta — reasoning_content fragment */
    SSE_DELTA_TOOLCALL_START,    /* toolcall_start — first id/name chunk */
    SSE_DELTA_TOOLCALL_DELTA,    /* toolcall_delta — arguments fragment */
    SSE_DELTA_TOOLCALL_END       /* toolcall_end — at stream end */
} sse_delta_kind;

typedef struct {
    sse_delta_kind kind;
    int content_index;  /* block index in the assistant content array (§5) */
    const char *text;   /* text delta, or the raw arguments fragment */
    const char *id;     /* toolcall: accumulated id */
    const char *name;   /* toolcall: accumulated name */
    const char *args;   /* toolcall: accumulated raw arguments JSON */
} sse_delta;

typedef void (*sse_delta_cb)(const sse_delta *d, void *userdata);


/* Free an api_response */
void api_response_free(api_response *resp);

/* Initialize an api_response */
void api_response_init(api_response *resp);

/* Send the pi-format messages array to the API and stream the response.
   The system prompt is prepended at request time — never persisted.
   max_tokens caps the completion when > 0 (0 = omit — the run loop's
   budget is enforced client-side, the commit generation caps at ~512).
   json_mode sends response_format {"type":"json_object"} — the commit
   generation's retry uses it to force a parseable reply.
   Returns 0 on success, -1 on error.
   resp is filled with parsed content. on_delta, when non-NULL, fires per
   text/toolcall delta with the block's contentIndex (§5) — the events
   stream's message_update source. */
int api_chat(const jb_config *cfg, const char *sys_prompt, cJSON *messages,
             cJSON *tools, long max_tokens, int json_mode,
             api_response *resp, sse_delta_cb on_delta, void *delta_userdata);

/* The curl child's pid while an api_chat stream is in flight (0 when no
   stream is active). The signal handler kills it to unblock the read. */
void api_set_curl_pid(pid_t pid);
pid_t api_curl_pid(void);

#endif
