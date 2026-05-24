/* config.h — configuration loading */
#ifndef JB_CONFIG_H
#define JB_CONFIG_H

typedef struct {
    char api_url[1024];
    char model[128];
    long max_tokens;
    long max_output_lines;
    long max_output_bytes;
} jb_config;

/* Load config from $XDG_CONFIG_HOME/jb/config.json.
   Returns 0 on success, -1 on error (missing file, missing API key).
   API key read from JB_API_KEY or OPENAI_API_KEY env var. */
int config_load(jb_config *cfg);

#endif
