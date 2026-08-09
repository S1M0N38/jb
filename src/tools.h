/* tools.h — tool definitions and implementations */
#ifndef JB_TOOLS_H
#define JB_TOOLS_H

#include "cJSON.h"
#include <sys/types.h>

/* Returns a cJSON array of tool definitions for the API request */
cJSON *tools_get_definitions(void);

/* Execute a tool call and return the result string.
   Caller must free the returned string. */
char *tool_execute(const char *name, const char *arguments);

/* Set output limits (from config) */
void tools_set_limits(long max_lines, long max_bytes);

/* Set session UUID for temp file naming */
void tools_set_session(const char *uuid);

/* Set path to jb binary for the jb tool */
void tools_set_jb_path(const char *path);

/* Set config file path for child jb inheritance */
void tools_set_config_path(const char *path);

/* The pid of the tool child currently executing (0 when none). The signal
   handler kills it to unblock the read — async-signal-safe. */
void tools_set_child_pid(pid_t pid);
pid_t tools_child_pid(void);

/* Non-zero when SIGINT/SIGTERM has been caught (defined in jb.c). */
int jb_signal_pending(void);

#endif
