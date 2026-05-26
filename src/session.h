/* session.h — session state management */
#ifndef JB_SESSION_H
#define JB_SESSION_H

#include <stdio.h>

#define JB_UUID_LEN 37  /* 36 chars + null */

typedef struct {
    char uuid[JB_UUID_LEN];
    char session_dir[4096];
    char state_path[4096];
    char log_path[4096];
    char metadata_path[4096];
    char title[128];           /* prompt-derived title */
    char started_at[32];      /* ISO 8601 timestamp */
    char working_dir[4096];   /* cwd at session start */
    char model[128];          /* model name */
    char parent[JB_UUID_LEN]; /* parent session UUID, empty string if none */
    FILE *log_fp;
    FILE *state_fp;
} jb_session;

/* Initialize a new session: generate UUID, create dirs, open files.
   Returns 0 on success, -1 on error. */
int session_init(jb_session *sess);

/* Append a JSON message line to state.jsonl.
   The json string is NOT freed. Returns 0 on success. */
int session_append_state(jb_session *sess, const char *json_line);

/* Append a raw SSE line to log.jsonl.
   Returns 0 on success. */
int session_append_log(jb_session *sess, const char *line);

/* Write initial metadata.json (status: running). Called after session_init().
   The prompt is used to derive a session title.
   Returns 0 on success, -1 on error. */
int session_write_metadata_init(jb_session *sess, const char *prompt,
                                const char *working_dir, const char *model);

/* Write final metadata.json (overwrite with completed status).
   Returns 0 on success, -1 on error. */
int session_write_metadata_close(jb_session *sess, const char *status,
                                 long tokens_used, int turns, int exit_code);

/* Set the parent UUID for this session. Call before session_write_metadata_init. */
void session_set_parent(jb_session *sess, const char *parent_uuid);

/* Close session files. */
void session_close(jb_session *sess);

#endif
