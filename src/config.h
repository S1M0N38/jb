/* config.h — configuration loading */
#ifndef JB_CONFIG_H
#define JB_CONFIG_H

#include <stddef.h>

/* Fixed limits — not configurable (the minimal config is api_url + model
   only). Hardcoded here so jb.c / tools.c share one source of truth. */
#define JB_MAX_TURNS          50      /* loop safety ceiling */
#define JB_TOKEN_BUDGET       500000L /* session stop-budget (client-side) */
#define JB_MAX_OUTPUT_LINES   2000L   /* tool output truncation */
#define JB_MAX_OUTPUT_BYTES   51200L  /* tool output truncation */

typedef struct {
    char api_url[1024];
    char model[128];
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

/* config_apply_override — apply one -c KEY=VALUE override through the
   registry. Unknown keys warn. Returns -1 on error (printed). */
int config_apply_override(jb_config *cfg, const char *key, const char *val);

/* config_validate — api_url and model are required (no built-in defaults).
   Prints a bootstrap hint on failure. Returns 0 if both are set. */
int config_validate(const jb_config *cfg);

#endif