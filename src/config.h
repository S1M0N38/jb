/* config.h — configuration loading */
#ifndef JB_CONFIG_H
#define JB_CONFIG_H

#include <stddef.h>

typedef struct {
    char api_url[1024];
    char model[128];
    long max_tokens;
    long max_output_lines;
    long max_output_bytes;
} jb_config;

/* Load config from given path, or $XDG_CONFIG_HOME/jb/config.json if NULL.
   Returns 0 on success, -1 on error (missing file, missing API key).
   API key read from JB_API_KEY or OPENAI_API_KEY env var.
   If config_path is set and file can't be loaded, prints error to stderr.
   After successful load, config_get_resolved_path() returns the path used.
   The repo's local .jb/config.json (when present) merges over the loaded
   file — git config --local analog. */
int config_load(jb_config *cfg, const char *config_path);

/* config_global_path — the global config file path:
   $XDG_CONFIG_HOME/jb/config.json (default ~/.config/jb/config.json). */
int config_global_path(char *out, size_t outlen);

/* Returns the resolved config file path from the last config_load call,
   or NULL if config hasn't been loaded yet. */
const char *config_get_resolved_path(void);

#endif
