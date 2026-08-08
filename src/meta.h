/* meta.h — phase 6 metadata verbs: status, log, show, ps, wait, path, config.
   All read-only over .jb/sessions/<uuid>/metadata.json — no API calls. */
#ifndef JB_META_H
#define JB_META_H

#include <stddef.h>

/* Shared CLI helpers (moved here from jb.c so verbs outside jb.c can use
   them): repo walk-up, ID argument resolution. */

/* path_is_dir / path_is_file — stat checks. */
int path_is_dir(const char *path);
int path_is_file(const char *path);

/* jb_find_repo — walk up from start looking for .jb/. Returns 0 with the
   repo root in out, or -1 when no repo encloses start. */
int jb_find_repo(const char *start, char *out, size_t outlen);

/* Resolve a session ID argument: "@" reads $JB_SESSION (unset →
   "jb: JB_SESSION not set"). Prints the reference error and returns -1 on
   failure, else 0 with the full uuid in out. */
int jb_resolve_id_arg(const char *repo_root, const char *arg,
                      char *out, size_t outlen);

/* jb path ID — print the absolute session directory. 0 · 1 not found · 2 usage */
int cmd_path(const char *id_arg);

/* jb show [ID] — pretty-print metadata.json (indent 2) to stdout; the
   metadata path on stderr. ID defaults to @. 0 · 1 not found/not set */
int cmd_show(const char *id_arg);

#endif
