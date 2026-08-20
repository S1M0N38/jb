/* config.c — configuration loading from XDG config path */
#include "config.h"
#include "meta.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

/* config_global_path — ~/.config/jb/config.json (or $XDG_CONFIG_HOME). */
int config_global_path(char *out, size_t outlen)
{
    snprintf(out, outlen, "%s/jb/config.json", xdg_config_home());
    return 0;
}

/* num_or_str — values are stored as strings by jb config; the reader
   coerces (git-style tolerance). */
static long num_or_str(const cJSON *v, long fallback)
{
    if (cJSON_IsNumber(v)) return (long)v->valuedouble;
    if (cJSON_IsString(v) && v->valuestring) return atol(v->valuestring);
    return fallback;
}

/* overlay_file — apply a config file's known keys onto cfg (missing file
   is a no-op). Used for the local .jb/config.json merge. */
static void overlay_file(jb_config *cfg, const char *path)
{
    char *json = read_file(path);
    if (!json) return;
    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) { cJSON_Delete(root); return; }

    cJSON *v;
    v = cJSON_GetObjectItemCaseSensitive(root, "api_url");
    if (cJSON_IsString(v) && v->valuestring)
        strncpy(cfg->api_url, v->valuestring, sizeof(cfg->api_url) - 1);
    v = cJSON_GetObjectItemCaseSensitive(root, "model");
    if (cJSON_IsString(v) && v->valuestring)
        strncpy(cfg->model, v->valuestring, sizeof(cfg->model) - 1);
    v = cJSON_GetObjectItemCaseSensitive(root, "max_tokens");
    cfg->max_tokens = num_or_str(v, cfg->max_tokens);
    v = cJSON_GetObjectItemCaseSensitive(root, "max_output_lines");
    cfg->max_output_lines = num_or_str(v, cfg->max_output_lines);
    v = cJSON_GetObjectItemCaseSensitive(root, "max_output_bytes");
    cfg->max_output_bytes = num_or_str(v, cfg->max_output_bytes);

    cJSON_Delete(root);
}

/* config_validate — api_url and model are required (no built-in defaults).
   Prints a bootstrap hint on failure. Called after the config merge and
   again after per-run -c overrides in cmd_run(). */
int config_validate(const jb_config *cfg)
{
    if (!cfg->api_url[0] || !cfg->model[0]) {
        fprintf(stderr,
                "jb: api_url and model are required (no built-in defaults)\n"
                "  jb config --global api_url https://opencode.ai/zen/go/v1\n"
                "  jb config --global model mimo-v2.5\n");
        return -1;
    }
    return 0;
}

int config_load(jb_config *cfg, const char *config_path)
{
    /* Defaults — limits only. api_url and model are required and have NO
       defaults: jb ships no endpoint or model identity (see jb.1). */
    memset(cfg, 0, sizeof(*cfg));
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
        } else {
            fprintf(stderr,
                    "jb: config: no config file at %s\n"
                    "  api_url and model are required (no built-in defaults)\n"
                    "  jb config --global api_url https://opencode.ai/zen/go/v1\n"
                    "  jb config --global model mimo-v2.5\n",
                    path);
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
    cfg->max_tokens = num_or_str(v, cfg->max_tokens);

    v = cJSON_GetObjectItemCaseSensitive(root, "max_output_lines");
    cfg->max_output_lines = num_or_str(v, cfg->max_output_lines);

    v = cJSON_GetObjectItemCaseSensitive(root, "max_output_bytes");
    cfg->max_output_bytes = num_or_str(v, cfg->max_output_bytes);

    cJSON_Delete(root);

    /* Save resolved config path for child inheritance */
    if (config_path) {
        strncpy(g_resolved_path, path, sizeof(g_resolved_path) - 1);
    }

    /* Phase 6: the repo's local .jb/config.json merges over the global
       (git config --local analog) — the effective view is the merged one. */
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) {
        char repo_root[4096];
        if (jb_find_repo(cwd, repo_root, sizeof(repo_root)) == 0) {
            char local[4096];
            snprintf(local, sizeof(local), "%s/.jb/config.json", repo_root);
            overlay_file(cfg, local);
        }
    }

    /* api_url and model must be configured — no silent defaults. */
    if (config_validate(cfg) != 0)
        return -1;

    /* API key is required from environment */
    const char *key = getenv("JB_API_KEY");
    if (!key || !key[0]) key = getenv("OPENAI_API_KEY");
    if (!key || !key[0]) {
        fprintf(stderr, "jb: no API key — set JB_API_KEY or OPENAI_API_KEY\n");
        return -1;
    }

    return 0;
}

const char *config_get_resolved_path(void)
{
    return g_resolved_path[0] ? g_resolved_path : NULL;
}
