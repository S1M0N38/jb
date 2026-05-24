/* tools.h — tool definitions and implementations */
#ifndef JB_TOOLS_H
#define JB_TOOLS_H

#include "cJSON.h"

/* Returns a cJSON array of tool definitions for the API request */
cJSON *tools_get_definitions(void);

/* Execute a tool call and return the result string.
   Caller must free the returned string. */
char *tool_execute(const char *name, const char *arguments);

/* Set output limits (from config) */
void tools_set_limits(long max_lines, long max_bytes);

#endif
