/* commit.h — jb commit ID [-m subject] [-m body] (plan phase 7) */
#ifndef JB_COMMIT_H
#define JB_COMMIT_H

/* Finalize a session: rules (refuse working, refuse uncommitted parent,
   -m/-m, amend), auto message (real API, JSON-only), metadata-only
   rewrite. 0 committed · 1 refused/not-found/generation-failed · 2 usage */
int cmd_commit(int argc, char **argv);

#endif
