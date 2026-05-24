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

/* Close session files. */
void session_close(jb_session *sess);

#endif
