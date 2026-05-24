/* prompt.c — system prompt assembly */
#include "prompt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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
        "- read: Read file contents\n"
        "- write: Create or overwrite files\n"
        "- edit: Make precise text replacements in files\n"
        "- bash: Execute shell commands\n\n"
        "Be concise. Execute. You can spawn a sub-agent by running `jb` as a bash command.\n\n");

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

    /* TODO: AGENTS.md walking from cwd up */
    /* TODO: Skills discovery and index */

    return prompt;
}
