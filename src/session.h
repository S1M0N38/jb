/* session.h — session storage: .jb/sessions/<uuid>/ (pi format) */
#ifndef JB_SESSION_H
#define JB_SESSION_H

#include <stdio.h>
#include "config.h"
#include "cJSON.h"

#define JB_UUID_LEN 37  /* 36 chars + null */

typedef struct {
    char uuid[JB_UUID_LEN];
    char session_dir[4096];
    char session_path[4096];    /* session.jsonl — the conversation (pi v3) */
    char events_path[4096];     /* events.jsonl — the live stream */
    char metadata_path[4096];   /* metadata.json — the jb index */
    char subject[256];          /* prompt first line */
    char started_at[40];        /* ISO-ms timestamp */
    char working_dir[4096];     /* cwd at session start */
    char author[JB_UUID_LEN];   /* creator: $JB_SESSION or "" (human) */
    jb_config cfg_snapshot;     /* config snapshot for metadata close */
    FILE *session_fp;
    FILE *events_fp;
} jb_session;

/* Initialize a new session under <repo_root>/.jb/sessions/<uuid>/:
   generate UUID, create the dir, open session.jsonl + events.jsonl.
   Returns 0 on success, -1 on error. */
int session_init(jb_session *sess, const char *repo_root);

/* Write the v3 session header as the first line of both session.jsonl
   and events.jsonl. Returns 0 on success, -1 on error. */
int session_write_header(jb_session *sess, const char *cwd);

/* Append a JSON line to session.jsonl. Returns 0 on success. */
int session_append_pi(jb_session *sess, const char *json_line);

/* Append a JSON line to events.jsonl. Returns 0 on success. */
int session_append_event(jb_session *sess, const char *json_line);

/* Write initial metadata.json (status: working, subject, config snapshot).
   Returns 0 on success, -1 on error. */
int session_write_metadata_init(jb_session *sess, const char *prompt,
                                const char *working_dir, const jb_config *cfg);

/* Write final metadata.json (status: completed|error + counters).
   Atomic: temp file + rename. Returns 0 on success, -1 on error. */
int session_write_metadata_close(jb_session *sess, const char *status,
                                 long tokens_used, int turns, int exit_code);

/* Set the author (creator session) — from $JB_SESSION env when spawned.
   Call before session_write_metadata_init. */
void session_set_author(jb_session *sess, const char *author);

/* Close session files. */
void session_close(jb_session *sess);

/* ---- Entry helpers ---- */

/* Generate an 8-hex entry id from /dev/urandom. */
void jb_id8(char *out, size_t outlen);

/* ISO 8601 UTC with milliseconds: "2026-08-06T00:33:50.332Z" */
void jb_iso8601_ms(char *out, size_t outlen);

/* Current time as epoch milliseconds. */
long jb_epoch_ms(void);

#endif
