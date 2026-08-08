/* api.h — HTTP/SSE communication with Chat Completions API */
#ifndef JB_API_H
#define JB_API_H

#include "config.h"
#include "cJSON.h"
#include "session.h"

/* Parsed state from a completed SSE stream */
typedef struct {
    char *text;             /* accumulated text content (final answer) */
    int finish_tool_calls;  /* 1 if finish_reason was "tool_calls" */
    cJSON *tool_calls_arr;  /* array of accumulated tool call objects */
    long total_tokens;      /* from usage field */
    long prompt_tokens;     /* usage: input tokens */
    long completion_tokens; /* usage: output tokens */
    long reasoning_tokens;  /* usage: reasoning tokens (0 if absent) */
} api_response;

/* Free an api_response */
void api_response_free(api_response *resp);

/* Initialize an api_response */
void api_response_init(api_response *resp);

/* Send the pi-format messages array to the API and stream the response.
   The system prompt is prepended at request time — never persisted.
   Returns 0 on success, -1 on error.
   resp is filled with parsed content. */
int api_chat(const jb_config *cfg, const char *sys_prompt, cJSON *messages,
             cJSON *tools, api_response *resp);

/* Copy the text streamed so far by the in-flight api_chat call into out
   ("" when no stream is active). For the SIGINT abort path. */
void api_stream_text_snapshot(char *out, size_t outlen);

#endif
