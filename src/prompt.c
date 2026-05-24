/* prompt.c — system prompt assembly */
#include "prompt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

static char *read_file_if_exists(const char *path)
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

static void str_append(char **dst, size_t *cap, size_t *len, const char *src)
{
    size_t slen = strlen(src);
    while (*len + slen + 1 > *cap) {
        *cap *= 2;
        *dst = realloc(*dst, *cap);
    }
    memcpy(*dst + *len, src, slen);
    *len += slen;
    (*dst)[*len] = '\0';
}

/* Walk from cwd upward looking for AGENTS.md, concatenate in order (root first) */
static void append_agents_md(char **dst, size_t *cap, size_t *len)
{
    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) return;

    /* Collect paths from cwd up to root */
    char *paths[128];
    int count = 0;

    char dir[4096];
    strncpy(dir, cwd, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';

    while (1) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/AGENTS.md", dir);
        char *content = read_file_if_exists(path);
        if (content) {
            free(content);
            paths[count++] = strdup(path);
        }

        if (strcmp(dir, "/") == 0) break;

        /* Go up one directory */
        char *last_slash = strrchr(dir, '/');
        if (last_slash == dir) {
            strcpy(dir, "/");
        } else if (last_slash) {
            *last_slash = '\0';
        } else {
            break;
        }
    }

    /* Append in reverse order (root first) */
    for (int i = count - 1; i >= 0; i--) {
        char *content = read_file_if_exists(paths[i]);
        if (content) {
            str_append(dst, cap, len, "\n");
            str_append(dst, cap, len, content);
            str_append(dst, cap, len, "\n");
            free(content);
        }
        free(paths[i]);
    }
}

/* Simple YAML frontmatter parser — extract name and description from SKILL.md */
typedef struct {
    char name[256];
    char description[1024];
    char filepath[4096];
} skill_info;

static int parse_frontmatter(const char *content, char *name, size_t name_sz, char *desc, size_t desc_sz)
{
    name[0] = '\0';
    desc[0] = '\0';

    /* Find --- delimiters */
    const char *start = strstr(content, "---");
    if (!start) return -1;
    start += 3;

    const char *end = strstr(start, "---");
    if (!end) return -1;

    /* Parse key: value lines between --- markers */
    const char *p = start;
    while (p < end) {
        /* Skip whitespace */
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        if (p >= end) break;

        const char *eol = strchr(p, '\n');
        if (!eol) eol = end;

        /* Find colon */
        const char *colon = memchr(p, ':', (size_t)(eol - p));
        if (colon) {
            size_t key_len = (size_t)(colon - p);
            const char *val = colon + 1;
            while (val < eol && (*val == ' ' || *val == '\t')) val++;
            size_t val_len = (size_t)(eol - val);
            /* Strip trailing whitespace from value */
            while (val_len > 0 && (val[val_len-1] == '\r' || val[val_len-1] == ' ' || val[val_len-1] == '\t'))
                val_len--;

            if (key_len == 4 && strncmp(p, "name", 4) == 0) {
                size_t copy = val_len < name_sz - 1 ? val_len : name_sz - 1;
                memcpy(name, val, copy);
                name[copy] = '\0';
            } else if (key_len == 11 && strncmp(p, "description", 11) == 0) {
                size_t copy = val_len < desc_sz - 1 ? val_len : desc_sz - 1;
                memcpy(desc, val, copy);
                desc[copy] = '\0';
            }
        }
        p = eol + 1;
    }

    return (name[0] || desc[0]) ? 0 : -1;
}

static int scan_skills_dir(const char *base_path, skill_info *skills, int max_skills, int *count)
{
    DIR *dir = opendir(base_path);
    if (!dir) return 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) && *count < max_skills) {
        if (ent->d_name[0] == '.') continue;

        char skill_path[4096];
        snprintf(skill_path, sizeof(skill_path), "%s/%s", base_path, ent->d_name);

        struct stat st;
        if (stat(skill_path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        /* Check for SKILL.md */
        char md_path[4096];
        snprintf(md_path, sizeof(md_path), "%s/SKILL.md", skill_path);

        char *content = read_file_if_exists(md_path);
        if (!content) continue;

        skill_info *sk = &skills[*count];
        if (parse_frontmatter(content, sk->name, sizeof(sk->name),
                              sk->description, sizeof(sk->description)) == 0) {
            /* Get absolute path */
            if (realpath(md_path, sk->filepath)) {
                (*count)++;
            }
        }
        free(content);
    }

    closedir(dir);
    return 0;
}

static void append_skills_index(char **dst, size_t *cap, size_t *len)
{
    skill_info skills[64];
    int count = 0;

    /* Global skills: $XDG_CONFIG_HOME/jb/agents/skills/ */
    {
        const char *xdg = getenv("XDG_CONFIG_HOME");
        char base[4096];
        if (xdg && xdg[0]) {
            snprintf(base, sizeof(base), "%s/jb/agents/skills", xdg);
        } else {
            snprintf(base, sizeof(base), "%s/.config/jb/agents/skills", getenv("HOME"));
        }
        scan_skills_dir(base, skills, 64, &count);
    }

    /* Local skills: .agents/skills/ from cwd */
    {
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd))) {
            char local_path[4096];
            snprintf(local_path, sizeof(local_path), "%s/.agents/skills", cwd);
            scan_skills_dir(local_path, skills, 64, &count);
        }
    }

    if (count == 0) return;

    str_append(dst, cap, len,
        "\n<available_skills>\n");

    for (int i = 0; i < count; i++) {
        char line[8192];
        snprintf(line, sizeof(line),
            "- name: %s\n  description: %s\n  location: %s\n\n",
            skills[i].name, skills[i].description, skills[i].filepath);
        str_append(dst, cap, len, line);
    }

    str_append(dst, cap, len, "</available_skills>\n");
}

char *prompt_build(void)
{
    size_t cap = 8192;
    size_t len = 0;
    char *prompt = malloc(cap);
    prompt[0] = '\0';

    /* Base prompt */
    str_append(&prompt, &cap, &len,
        "You are jb, a minimal agentic coding assistant. You help users by reading files, "
        "executing commands, editing code, and writing new files.\n\n"
        "You have four tools: read, write, edit, bash.\n"
        "- read: Read file contents (with line numbers, offset/limit support)\n"
        "- write: Create or overwrite files (creates parent dirs)\n"
        "- edit: Make precise text replacements in files\n"
        "- bash: Execute shell commands (with optional timeout)\n\n"
        "Be concise. Execute. You can spawn a sub-agent by running `jb` as a bash command.\n"
        "To read your own documentation, run `man jb | col -b`.\n\n");

    /* Current date */
    char date_buf[64];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(date_buf, sizeof(date_buf), "Current date: %Y-%m-%d\n", t);
    str_append(&prompt, &cap, &len, date_buf);

    /* Current working directory */
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) {
        str_append(&prompt, &cap, &len, "Current working directory: ");
        str_append(&prompt, &cap, &len, cwd);
        str_append(&prompt, &cap, &len, "\n\n");
    }

    /* AGENTS.md from cwd upward */
    append_agents_md(&prompt, &cap, &len);

    /* Skills index */
    append_skills_index(&prompt, &cap, &len);

    return prompt;
}
