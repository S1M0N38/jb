#ifndef JB_UI_H
#define JB_UI_H
/* ui.h — jb ui: the read-only session forest viewer (localhost HTTP).
   Starts a tiny HTTP server on 127.0.0.1 serving the embedded ui/ assets
   plus two JSON endpoints, both read-only over the sessions' metadata:
     GET /api/sessions        → array of {uuid,status,subject,author,
                                parent,started_ms,ended_ms,exit_code}
     GET /api/session/<uuid>  → the raw metadata.json of one session
   `jb ui --dev` serves ui/ from the repo root instead of the embedded
   copy (iteration loop). */
int cmd_ui(int argc, char **argv);
#endif
