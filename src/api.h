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
} api_response;

/* Free an api_response */
void api_response_free(api_response *resp);

/* Initialize an api_response */
void api_response_init(api_response *resp);

/* Send messages array to the API and stream the response.
   Returns 0 on success, -1 on error.
   resp is filled with parsed content. */
int api_chat(const jb_config *cfg, cJSON *messages, cJSON *tools, api_response *resp);

#endif
