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
    char author[JB_UUID_LEN];   /* creator: --seed / $JB_SESSION or "" (human) */
    char parent[JB_UUID_LEN];   /* parent session uuid — set only by --fork */
    char parent_path[4096];     /* parent session.jsonl path (header parentSession) */
    jb_config cfg_snapshot;     /* config snapshot for metadata close */
    char last_entry_id[16];     /* id of the last appended entry (parentId chain) */
    char **used_ids;            /* collision check for generated entry ids */
    int used_n;
    int used_cap;
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

/* Wrap a pi-format message object (§3.3) into a message entry and append it
   to session.jsonl: assigns a collision-checked 8-hex id, the parentId chain
   (null for the first entry, else the previous entry's id), and an ISO-ms
   timestamp. The message is added BY REFERENCE — the caller keeps ownership
   (typically via the in-memory messages array).
   Returns 0 on success, -1 on error. */
int session_append_message(jb_session *sess, cJSON *message);

/* Write initial metadata.json (status: working, subject, config snapshot).
   Returns 0 on success, -1 on error. */
int session_write_metadata_init(jb_session *sess, const char *prompt,
                                const char *working_dir, const jb_config *cfg);

/* Write final metadata.json (status: completed|error + counters).
   Atomic: temp file + rename. Returns 0 on success, -1 on error. */
int session_write_metadata_close(jb_session *sess, const char *status,
                                 long tokens_used, int turns, int exit_code);

/* Heartbeat: rewrite metadata.json with a fresh last_activity while the
   session is working. Atomic: temp file + rename. Returns 0 on success. */
int session_write_metadata_heartbeat(jb_session *sess);

/* Set the author (creator session) — from --seed or $JB_SESSION env when
   spawned. Call before session_write_metadata_init. */
void session_set_author(jb_session *sess, const char *author);

/* Set the parent session (--fork): the parent uuid (metadata "parent") and
   the absolute path of its session.jsonl (header "parentSession").
   Call before session_write_header / session_write_metadata_init. */
void session_set_parent(jb_session *sess, const char *uuid, const char *session_path);

/* Resolve a session ID against .jb/sessions/<uuid>/ directory names:
   a full uuid or a unique 4+ hex prefix. On success writes the full uuid
   to out and returns 0. Returns 1 (not found — err holds "no session …")
   or 2 (ambiguous — err holds "ambiguous id … (candidates)"). */
int session_resolve(const char *repo_root, const char *id,
                    char *out, size_t outlen, char *err, size_t errlen);

/* Load a pi-format session.jsonl (v3) into a messages array of pi-format
   message objects (deep copies of each entry's message, §4.1). Validates
   the header; tolerates unparseable lines. Trims the dangling tail: drops
   everything after the last complete assistant message (stopReason "stop",
   or "toolUse" with all its toolResults present) — an interrupted source
   session never leaks unpaired tool calls/results into the fork. With no
   complete assistant message the fork starts with an empty history.
   Returns the number of messages kept, or -1 when the file is missing or
   the header is invalid. */
int session_load_pi(const char *session_path, cJSON *messages);

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
