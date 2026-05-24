/* tools.c — tool definitions and implementations */
#include "tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

cJSON *tools_get_definitions(void)
{
    cJSON *arr = cJSON_CreateArray();

    /* read tool */
    {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "type", "function");
        cJSON *fn = cJSON_CreateObject();
        cJSON_AddStringToObject(fn, "name", "read");
        cJSON_AddStringToObject(fn, "description",
            "Read the contents of a file. Supports text files. "
            "Output is truncated to 2000 lines or 50KB. "
            "Use offset/limit for large files.");
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "type", "object");
        cJSON *required = cJSON_CreateArray();
        cJSON_AddItemToArray(required, cJSON_CreateString("path"));
        cJSON_AddItemToObject(params, "required", required);
        cJSON *props = cJSON_CreateObject();
        {
            cJSON *p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "type", "string");
            cJSON_AddStringToObject(p, "description", "Path to the file to read");
            cJSON_AddItemToObject(props, "path", p);
        }
        {
            cJSON *p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "type", "integer");
            cJSON_AddStringToObject(p, "description", "Line number to start reading from (1-indexed)");
            cJSON_AddItemToObject(props, "offset", p);
        }
        {
            cJSON *p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "type", "integer");
            cJSON_AddStringToObject(p, "description", "Maximum number of lines to read");
            cJSON_AddItemToObject(props, "limit", p);
        }
        cJSON_AddItemToObject(params, "properties", props);
        cJSON_AddItemToObject(fn, "parameters", params);
        cJSON_AddItemToObject(t, "function", fn);
        cJSON_AddItemToArray(arr, t);
    }

    /* write tool */
    {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "type", "function");
        cJSON *fn = cJSON_CreateObject();
        cJSON_AddStringToObject(fn, "name", "write");
        cJSON_AddStringToObject(fn, "description",
            "Write content to a file. Creates parent directories. Overwrites if exists.");
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "type", "object");
        cJSON *required = cJSON_CreateArray();
        cJSON_AddItemToArray(required, cJSON_CreateString("path"));
        cJSON_AddItemToArray(required, cJSON_CreateString("content"));
        cJSON_AddItemToObject(params, "required", required);
        cJSON *props = cJSON_CreateObject();
        {
            cJSON *p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "type", "string");
            cJSON_AddStringToObject(p, "description", "Path to the file");
            cJSON_AddItemToObject(props, "path", p);
        }
        {
            cJSON *p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "type", "string");
            cJSON_AddStringToObject(p, "description", "Content to write");
            cJSON_AddItemToObject(props, "content", p);
        }
        cJSON_AddItemToObject(params, "properties", props);
        cJSON_AddItemToObject(fn, "parameters", params);
        cJSON_AddItemToObject(t, "function", fn);
        cJSON_AddItemToArray(arr, t);
    }

    /* edit tool */
    {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "type", "function");
        cJSON *fn = cJSON_CreateObject();
        cJSON_AddStringToObject(fn, "name", "edit");
        cJSON_AddStringToObject(fn, "description",
            "Edit a file using exact text replacement. "
            "Each edit's oldText must match a unique region. No overlapping edits.");
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "type", "object");
        cJSON *required = cJSON_CreateArray();
        cJSON_AddItemToArray(required, cJSON_CreateString("path"));
        cJSON_AddItemToArray(required, cJSON_CreateString("edits"));
        cJSON_AddItemToObject(params, "required", required);
        cJSON *props = cJSON_CreateObject();
        {
            cJSON *p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "type", "string");
            cJSON_AddStringToObject(p, "description", "Path to the file");
            cJSON_AddItemToObject(props, "path", p);
        }
        {
            cJSON *p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "type", "array");
            cJSON_AddStringToObject(p, "description",
                "Array of {oldText, newText} replacements");
            cJSON_AddItemToObject(props, "edits", p);
        }
        cJSON_AddItemToObject(params, "properties", props);
        cJSON_AddItemToObject(fn, "parameters", params);
        cJSON_AddItemToObject(t, "function", fn);
        cJSON_AddItemToArray(arr, t);
    }

    /* bash tool */
    {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "type", "function");
        cJSON *fn = cJSON_CreateObject();
        cJSON_AddStringToObject(fn, "name", "bash");
        cJSON_AddStringToObject(fn, "description",
            "Execute a bash command. stdout+stderr merged. "
            "Output truncated at config limits. Timeout optional.");
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "type", "object");
        cJSON *required = cJSON_CreateArray();
        cJSON_AddItemToArray(required, cJSON_CreateString("command"));
        cJSON_AddItemToObject(params, "required", required);
        cJSON *props = cJSON_CreateObject();
        {
            cJSON *p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "type", "string");
            cJSON_AddStringToObject(p, "description", "Bash command to execute");
            cJSON_AddItemToObject(props, "command", p);
        }
        {
            cJSON *p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "type", "integer");
            cJSON_AddStringToObject(p, "description", "Timeout in seconds");
            cJSON_AddItemToObject(props, "timeout", p);
        }
        cJSON_AddItemToObject(params, "properties", props);
        cJSON_AddItemToObject(fn, "parameters", params);
        cJSON_AddItemToObject(t, "function", fn);
        cJSON_AddItemToArray(arr, t);
    }

    return arr;
}

/* ---- Tool implementations ---- */

static char *read_file_contents(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        char *err = malloc(256);
        snprintf(err, 256, "Error: cannot open file '%s'", path);
        return err;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return strdup("Error: out of memory"); }

    size_t nread = fread(buf, 1, (size_t)len, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

static char *tool_read(const char *arguments)
{
    cJSON *args = cJSON_Parse(arguments);
    if (!args) return strdup("Error: malformed arguments");

    cJSON *path_j = cJSON_GetObjectItemCaseSensitive(args, "path");
    if (!path_j || !cJSON_IsString(path_j)) {
        cJSON_Delete(args);
        return strdup("Error: 'path' is required");
    }

    const char *path = path_j->valuestring;
    char *content = read_file_contents(path);

    /* TODO: implement offset/limit, line numbers, truncation */

    cJSON_Delete(args);
    return content;
}

static char *tool_write(const char *arguments)
{
    cJSON *args = cJSON_Parse(arguments);
    if (!args) return strdup("Error: malformed arguments");

    cJSON *path_j = cJSON_GetObjectItemCaseSensitive(args, "path");
    cJSON *content_j = cJSON_GetObjectItemCaseSensitive(args, "content");

    if (!path_j || !cJSON_IsString(path_j)) {
        cJSON_Delete(args);
        return strdup("Error: 'path' is required");
    }
    if (!content_j || !cJSON_IsString(content_j)) {
        cJSON_Delete(args);
        return strdup("Error: 'content' is required");
    }

    /* Create parent directories with mkdir -p */
    char cmd[4096];
    const char *path = path_j->valuestring;

    /* Extract directory part */
    char dir[4096];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (dir[0]) {
            snprintf(cmd, sizeof(cmd), "mkdir -p '%s' 2>/dev/null", dir);
            system(cmd);
        }
    }

    /* Write content — use a temp file approach to avoid shell escaping */
    char tmpfile[256];
    snprintf(tmpfile, sizeof(tmpfile), "/tmp/jb-write-%ld.tmp", (long)getpid());

    FILE *tf = fopen(tmpfile, "w");
    if (!tf) {
        cJSON_Delete(args);
        return strdup("Error: cannot create temp file");
    }

    /* Write the content directly */
    const char *content = content_j->valuestring;
    fwrite(content, 1, strlen(content), tf);
    fclose(tf);

    snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", tmpfile, path);
    int ret = system(cmd);
    remove(tmpfile);

    cJSON_Delete(args);

    if (ret == 0) {
        return strdup("File written successfully.");
    } else {
        return strdup("Error: failed to write file");
    }
}

static char *tool_edit(const char *arguments)
{
    cJSON *args = cJSON_Parse(arguments);
    if (!args) return strdup("Error: malformed arguments");

    cJSON *path_j = cJSON_GetObjectItemCaseSensitive(args, "path");
    cJSON *edits_j = cJSON_GetObjectItemCaseSensitive(args, "edits");

    if (!path_j || !cJSON_IsString(path_j)) {
        cJSON_Delete(args);
        return strdup("Error: 'path' is required");
    }
    if (!edits_j || !cJSON_IsArray(edits_j)) {
        cJSON_Delete(args);
        return strdup("Error: 'edits' must be an array");
    }

    const char *path = path_j->valuestring;
    char *content = read_file_contents(path);
    if (!content) {
        cJSON_Delete(args);
        char *err = malloc(256);
        snprintf(err, 256, "Error: cannot read file '%s'", path);
        return err;
    }

    /* Apply each edit */
    int n = cJSON_GetArraySize(edits_j);
    for (int i = 0; i < n; i++) {
        cJSON *edit = cJSON_GetArrayItem(edits_j, i);
        cJSON *old_j = cJSON_GetObjectItemCaseSensitive(edit, "oldText");
        cJSON *new_j = cJSON_GetObjectItemCaseSensitive(edit, "newText");

        if (!old_j || !cJSON_IsString(old_j) || !new_j || !cJSON_IsString(new_j)) {
            free(content);
            cJSON_Delete(args);
            return strdup("Error: each edit needs 'oldText' and 'newText'");
        }

        const char *old_text = old_j->valuestring;
        const char *new_text = new_j->valuestring;

        /* Find and replace the first occurrence */
        char *pos = strstr(content, old_text);
        if (!pos) {
            free(content);
            cJSON_Delete(args);
            char *err = malloc(1024);
            snprintf(err, 1024,
                "Error: oldText not found in file (edit %d)", i + 1);
            return err;
        }

        /* Build new content */
        size_t old_len = strlen(old_text);
        size_t new_len = strlen(new_text);
        size_t content_len = strlen(content);
        size_t prefix_len = (size_t)(pos - content);
        size_t new_size = content_len - old_len + new_len + 1;

        char *new_content = malloc(new_size);
        memcpy(new_content, content, prefix_len);
        memcpy(new_content + prefix_len, new_text, new_len);
        memcpy(new_content + prefix_len + new_len,
               pos + old_len, content_len - prefix_len - old_len);
        new_content[new_size - 1] = '\0';

        free(content);
        content = new_content;
    }

    /* Write modified content back */
    FILE *f = fopen(path, "w");
    if (!f) {
        free(content);
        cJSON_Delete(args);
        return strdup("Error: cannot write file");
    }
    fwrite(content, 1, strlen(content), f);
    fclose(f);

    free(content);
    cJSON_Delete(args);
    return strdup("File edited successfully.");
}

static char *tool_bash(const char *arguments)
{
    cJSON *args = cJSON_Parse(arguments);
    if (!args) return strdup("Error: malformed arguments");

    cJSON *cmd_j = cJSON_GetObjectItemCaseSensitive(args, "command");
    if (!cmd_j || !cJSON_IsString(cmd_j)) {
        cJSON_Delete(args);
        return strdup("Error: 'command' is required");
    }

    /* TODO: implement timeout, truncation, temp file overflow */
    const char *cmd = cmd_j->valuestring;

    /* Merge stdout+stderr with 2>&1 */
    char full_cmd[8192];
    snprintf(full_cmd, sizeof(full_cmd), "%s 2>&1", cmd);

    FILE *fp = popen(full_cmd, "r");
    if (!fp) {
        cJSON_Delete(args);
        return strdup("Error: cannot execute command");
    }

    /* Read output */
    size_t cap = 65536;
    char *output = malloc(cap);
    size_t total = 0;
    char buf[4096];
    size_t nread;
    while ((nread = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (total + nread + 1 > cap) {
            cap *= 2;
            output = realloc(output, cap);
        }
        memcpy(output + total, buf, nread);
        total += nread;
    }
    output[total] = '\0';

    int status = pclose(fp);
    cJSON_Delete(args);

    /* TODO: truncate if too large, save overflow to temp file */

    /* Append exit code info */
    char *result = malloc(total + 128);
    int exit_code = WEXITSTATUS(status);
    if (exit_code != 0) {
        snprintf(result, total + 128, "%s\n[exit code: %d]", output, exit_code);
    } else {
        memcpy(result, output, total + 1);
    }
    free(output);

    return result;
}

char *tool_execute(const char *name, const char *arguments)
{
    if (strcmp(name, "read") == 0)  return tool_read(arguments);
    if (strcmp(name, "write") == 0) return tool_write(arguments);
    if (strcmp(name, "edit") == 0)  return tool_edit(arguments);
    if (strcmp(name, "bash") == 0)  return tool_bash(arguments);

    char *err = malloc(128);
    snprintf(err, 128, "Error: unknown tool '%s'", name);
    return err;
}
