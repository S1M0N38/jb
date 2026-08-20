/* config.c — configuration loading and merging.
 *
 * The config is deliberately minimal: exactly two keys, both required —
 * api_url (where to send requests) and model (which model). Every limit
 * (turn ceiling, token budget, tool output truncation) is a hardcoded
 * constant (see config.h); no endpoint or model identity is baked in.
 *
 * Unknown keys warn (typos are loud; forward-compat is preserved).
 */
#include "config.h"
#include "meta.h"
#include "cJSON.h"
#include <stddef.h>
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

/* ---- the key registry: the single source of truth for config keys.
   Minimal today (api_url + model); add keys here, not by hand-editing
   parse/override code in three files. ---- */

typedef struct {
    const char *name;  /* config key */
    size_t offset;     /* offsetof(jb_config, member) */
    size_t size;       /* buffer size */
    unsigned required:1;
} cfg_key_def;

#define KEY(name, member, req) \
    { name, offsetof(jb_config, member), sizeof(((jb_config *)0)->member), req }

static const cfg_key_def g_keys[] = {
    KEY("api_url", api_url, 1),
    KEY("model",   model,   1),
};
#define N_KEYS (sizeof(g_keys) / sizeof(g_keys[0]))

static const cfg_key_def *find_key(const char *name)
{
    for (size_t i = 0; i < N_KEYS; i++)
        if (strcmp(g_keys[i].name, name) == 0) return &g_keys[i];
    return NULL;
}

/* apply_json — iterate a config file's keys through the registry.
   Unknown keys warn. Returns -1 (with a stderr message) on a type error. */
static int apply_json(jb_config *cfg, cJSON *root, const char *label)
{
    for (cJSON *it = root->child; it; it = it->next) {
        if (!it->string) continue;
        const cfg_key_def *k = find_key(it->string);
        if (!k) {
            fprintf(stderr, "jb: config: %s: unknown key '%s' (ignored)\n",
                    label, it->string);
            continue;
        }
        if (!cJSON_IsString(it) || !it->valuestring) {
            fprintf(stderr, "jb: config: %s: %s: expected a string\n",
                    label, k->name);
            return -1;
        }
        char *dst = (char *)cfg + k->offset;
        strncpy(dst, it->valuestring, k->size - 1);
        dst[k->size - 1] = '\0';
    }
    return 0;
}

/* config_apply_override — apply one -c KEY=VALUE override through the
   registry. Unknown keys warn. Returns -1 on error (printed). */
int config_apply_override(jb_config *cfg, const char *key, const char *val)
{
    const cfg_key_def *k = find_key(key);
    if (!k) {
        fprintf(stderr, "jb: config: unknown key '%s' (ignored)\n", key);
        return 0;
    }
    char *dst = (char *)cfg + k->offset;
    strncpy(dst, val, k->size - 1);
    dst[k->size - 1] = '\0';
    return 0;
}

/* config_validate — api_url and model are required; jb ships no default
   endpoint or model identity. Prints a bootstrap hint on failure. Also
   called again after -c overrides in cmd_run(). */
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
    memset(cfg, 0, sizeof(*cfg));

    /* Build config path */
    char path[4096];
    if (config_path) {
        /* Resolve to absolute path so relative --config works from any cwd */
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
        return -1;
    }

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) {
        fprintf(stderr, "jb: config: %s: invalid JSON\n", path);
        return -1;
    }
    if (apply_json(cfg, root, path) != 0) { cJSON_Delete(root); return -1; }
    cJSON_Delete(root);

    /* Save resolved config path for child inheritance */
    if (config_path) {
        strncpy(g_resolved_path, path, sizeof(g_resolved_path) - 1);
    }

    /* The repo's local .jb/config.json merges over the global
       (git config --local analog) — the effective view is the merged one. */
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) {
        char repo_root[4096];
        if (jb_find_repo(cwd, repo_root, sizeof(repo_root)) == 0) {
            char local[4096];
            snprintf(local, sizeof(local), "%s/.jb/config.json", repo_root);
            char *j2 = read_file(local);
            if (j2) {
                cJSON *r2 = cJSON_Parse(j2);
                free(j2);
                if (r2) {
                    if (apply_json(cfg, r2, local) != 0) { cJSON_Delete(r2); return -1; }
                    cJSON_Delete(r2);
                }
            }
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