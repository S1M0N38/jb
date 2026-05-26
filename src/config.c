/* config.c — configuration loading from XDG config path */
#include "config.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Track resolved config path for child inheritance */
static char g_resolved_path[4096] = "";

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t nread = fread(buf, 1, (size_t)len, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

static const char *xdg_config_home(void)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) return xdg;
    static char buf[4096];
    snprintf(buf, sizeof(buf), "%s/.config", getenv("HOME"));
    return buf;
}

int config_load(jb_config *cfg, const char *config_path)
{
    /* Defaults */
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->api_url, "https://api.openai.com/v1", sizeof(cfg->api_url) - 1);
    strncpy(cfg->model, "gpt-4.1", sizeof(cfg->model) - 1);
    cfg->max_tokens = 500000;
    cfg->max_output_lines = 2000;
    cfg->max_output_bytes = 51200;

    /* Build config path */
    char path[4096];
    if (config_path) {
        /* Resolve to absolute path */
        if (!realpath(config_path, path)) {
            fprintf(stderr, "jb: config: %s: No such file or directory\n", config_path);
            return -1;
        }
    } else {
        snprintf(path, sizeof(path), "%s/jb/config.json", xdg_config_home());
    }

    char *json = read_file(path);
    if (!json) {
        if (config_path) {
            fprintf(stderr, "jb: config: %s: cannot read file\n", config_path);
        }
        return -1;  /* config file must exist */
    }

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) return -1;

    cJSON *v;

    v = cJSON_GetObjectItemCaseSensitive(root, "api_url");
    if (cJSON_IsString(v) && v->valuestring)
        strncpy(cfg->api_url, v->valuestring, sizeof(cfg->api_url) - 1);

    v = cJSON_GetObjectItemCaseSensitive(root, "model");
    if (cJSON_IsString(v) && v->valuestring)
        strncpy(cfg->model, v->valuestring, sizeof(cfg->model) - 1);

    v = cJSON_GetObjectItemCaseSensitive(root, "max_tokens");
    if (cJSON_IsNumber(v))
        cfg->max_tokens = (long)v->valuedouble;

    v = cJSON_GetObjectItemCaseSensitive(root, "max_output_lines");
    if (cJSON_IsNumber(v))
        cfg->max_output_lines = (long)v->valuedouble;

    v = cJSON_GetObjectItemCaseSensitive(root, "max_output_bytes");
    if (cJSON_IsNumber(v))
        cfg->max_output_bytes = (long)v->valuedouble;

    cJSON_Delete(root);

    /* Save resolved config path for child inheritance */
    if (config_path) {
        strncpy(g_resolved_path, path, sizeof(g_resolved_path) - 1);
    }

    /* API key is required from environment */
    const char *key = getenv("JB_API_KEY");
    if (!key || !key[0]) key = getenv("OPENAI_API_KEY");
    if (!key || !key[0]) return -1;

    return 0;
}

const char *config_get_resolved_path(void)
{
    return g_resolved_path[0] ? g_resolved_path : NULL;
}
