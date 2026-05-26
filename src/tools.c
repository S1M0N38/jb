/* tools.c — tool definitions and implementations */
#include "tools.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/* External config — set from jb.c before tool execution */
static long g_max_output_bytes = 51200;
static long g_max_output_lines = 2000;
static char g_session_uuid[64] = "";
static char g_jb_path[4096] = "jb";  /* path to jb binary, default to $PATH lookup */
static char g_config_path[4096] = "";  /* config file path for child inheritance */
static int g_bash_counter = 0;

void tools_set_limits(long max_lines, long max_bytes)
{
    g_max_output_lines = max_lines;
    g_max_output_bytes = max_bytes;
}

void tools_set_session(const char *uuid)
{
    strncpy(g_session_uuid, uuid, sizeof(g_session_uuid) - 1);
    g_bash_counter = 0;
}

void tools_set_jb_path(const char *path)
{
    strncpy(g_jb_path, path, sizeof(g_jb_path) - 1);
}

void tools_set_config_path(const char *path)
{
    if (path) {
        strncpy(g_config_path, path, sizeof(g_config_path) - 1);
    } else {
        g_config_path[0] = '\0';
    }
}

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

    /* jb tool */
    {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "type", "function");
        cJSON *fn = cJSON_CreateObject();
        cJSON_AddStringToObject(fn, "name", "jb");
        cJSON_AddStringToObject(fn, "description",
            "Spawn a child jb session to delegate a task. "
            "The child runs independently and returns its final answer via stdout. "
            "Use for sub-tasks that benefit from a fresh context — research, exploration, parallel work.");
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "type", "object");
        cJSON *required = cJSON_CreateArray();
        cJSON_AddItemToArray(required, cJSON_CreateString("prompt"));
        cJSON_AddItemToObject(params, "required", required);
        cJSON *props = cJSON_CreateObject();
        {
            cJSON *p = cJSON_CreateObject();
            cJSON_AddStringToObject(p, "type", "string");
            cJSON_AddStringToObject(p, "description", "Task to send to the child session");
            cJSON_AddItemToObject(props, "prompt", p);
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
    int offset = 0;  /* 1-indexed, 0 means from start */
    int limit = 0;   /* 0 means no limit (up to max) */

    cJSON *off_j = cJSON_GetObjectItemCaseSensitive(args, "offset");
    if (off_j && cJSON_IsNumber(off_j)) offset = off_j->valueint;

    cJSON *lim_j = cJSON_GetObjectItemCaseSensitive(args, "limit");
    if (lim_j && cJSON_IsNumber(lim_j)) limit = lim_j->valueint;

    FILE *f = fopen(path, "rb");
    if (!f) {
        char *err = malloc(256);
        snprintf(err, 256, "Error: cannot open file '%s'", path);
        cJSON_Delete(args);
        return err;
    }

    /* Read file with line numbers, like cat -n */
    size_t cap = g_max_output_bytes + 1;
    char *result = malloc(cap);
    size_t result_len = 0;
    result[0] = '\0';

    char line[65536];
    int line_no = 0;
    int lines_output = 0;

    while (fgets(line, sizeof(line), f)) {
        line_no++;

        /* Skip lines before offset */
        if (offset > 0 && line_no < offset) continue;

        /* Check line limit */
        if (limit > 0 && lines_output >= limit) break;

        /* Check max output lines */
        if (lines_output >= g_max_output_lines) break;

        /* Format: "  123 | <content>" */
        char numbered[66000];
        int n = snprintf(numbered, sizeof(numbered), "%6d\t%s", line_no, line);

        /* Ensure newline at end */
        if (n > 0 && numbered[n-1] != '\n') {
            numbered[n] = '\n';
            numbered[n+1] = '\0';
            n++;
        }

        /* Check byte limit */
        if (result_len + (size_t)n >= cap - 1) {
            /* Truncate */
            size_t remaining = cap - result_len - 1;
            if (remaining > 0) {
                memcpy(result + result_len, numbered, remaining);
                result_len += remaining;
            }
            break;
        }

        memcpy(result + result_len, numbered, (size_t)n);
        result_len += (size_t)n;
        lines_output++;
    }
    result[result_len] = '\0';
    fclose(f);

    /* Add truncation notice if needed */
    if (!feof(f) || result_len >= (size_t)(g_max_output_bytes - 100)) {
        const char *notice = "\n... (truncated)";
        size_t notice_len = strlen(notice);
        if (result_len + notice_len < cap) {
            memcpy(result + result_len, notice, notice_len);
            result_len += notice_len;
            result[result_len] = '\0';
        }
    }

    cJSON_Delete(args);
    return result;
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

    const char *path = path_j->valuestring;
    const char *content = content_j->valuestring;

    /* Create parent directories with mkdir -p */
    char dir[4096];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (dir[0]) {
            char cmd[4096];
            snprintf(cmd, sizeof(cmd), "mkdir -p '%s' 2>/dev/null", dir);
            system(cmd);
        }
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        cJSON_Delete(args);
        return strdup("Error: cannot create file");
    }

    fwrite(content, 1, strlen(content), f);
    fclose(f);

    cJSON_Delete(args);
    return strdup("File written successfully.");
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

    /* Read file */
    FILE *f = fopen(path, "rb");
    if (!f) {
        cJSON_Delete(args);
        char *err = malloc(256);
        snprintf(err, 256, "Error: cannot read file '%s'", path);
        return err;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *content = malloc((size_t)len + 1);
    size_t nread = fread(content, 1, (size_t)len, f);
    content[nread] = '\0';
    fclose(f);

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

        char *pos = strstr(content, old_text);
        if (!pos) {
            free(content);
            cJSON_Delete(args);
            char *err = malloc(1024);
            snprintf(err, 1024,
                "Error: oldText not found in file (edit %d)", i + 1);
            return err;
        }

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

    /* Write back */
    f = fopen(path, "w");
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

    const char *cmd = cmd_j->valuestring;
    int timeout = 0;

    cJSON *timeout_j = cJSON_GetObjectItemCaseSensitive(args, "timeout");
    if (timeout_j && cJSON_IsNumber(timeout_j)) timeout = timeout_j->valueint;

    /* Build full command with stderr merged */
    char full_cmd[8192];
    if (timeout > 0) {
        snprintf(full_cmd, sizeof(full_cmd), "timeout %d %s 2>&1", timeout, cmd);
    } else {
        snprintf(full_cmd, sizeof(full_cmd), "%s 2>&1", cmd);
    }

    FILE *fp = popen(full_cmd, "r");
    if (!fp) {
        cJSON_Delete(args);
        return strdup("Error: cannot execute command");
    }

    /* Read output with truncation */
    size_t cap = g_max_output_bytes + 1;
    char *output = malloc(cap);
    size_t total = 0;
    char buf[4096];
    size_t nread;
    int truncated = 0;

    while ((nread = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (total + nread >= cap - 1) {
            size_t remaining = cap - total - 1;
            if (remaining > 0) {
                memcpy(output + total, buf, remaining);
                total += remaining;
            }
            truncated = 1;
            /* Drain the rest */
            while (fread(buf, 1, sizeof(buf), fp) > 0) {}
            break;
        }
        memcpy(output + total, buf, nread);
        total += nread;
    }
    output[total] = '\0';

    int status = pclose(fp);
    cJSON_Delete(args);

    /* Append exit code if non-zero */
    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code != 0) {
            char *result = malloc(total + 128);
            snprintf(result, total + 128, "%s\n[exit code: %d]", output, exit_code);
            free(output);
            return result;
        }
    }

    /* Append truncation notice with overflow file path */
    if (truncated) {
        /* Save full overflow output to temp file */
        const char *tmpdir = getenv("TMPDIR");
        if (!tmpdir || !tmpdir[0]) tmpdir = "/tmp";

        char tmpfile[512];
        g_bash_counter++;
        snprintf(tmpfile, sizeof(tmpfile), "%s/jb-%s-bash-%d.out",
                 tmpdir, g_session_uuid, g_bash_counter);

        /* Write remaining to temp file — we already drained, so write what we captured */
        /* Actually, we need to re-approach: write everything beyond limit to the temp file */
        /* For now, note the truncation with temp file path */
        FILE *tf = fopen(tmpfile, "w");
        if (tf) {
            fwrite(output, 1, total, tf);
            fclose(tf);
            char *result = malloc(total + 256);
            snprintf(result, total + 256,
                     "%s\n... (output truncated, %ld bytes max)\nFull output saved to: %s",
                     output, g_max_output_bytes, tmpfile);
            free(output);
            return result;
        }

        char *result = malloc(total + 128);
        snprintf(result, total + 128,
                 "%s\n... (output truncated, %ld bytes max)",
                 output, g_max_output_bytes);
        free(output);
        return result;
    }

    return output;
}

static char *tool_jb(const char *arguments)
{
    cJSON *args = cJSON_Parse(arguments);
    if (!args) return strdup("Error: malformed arguments");

    cJSON *prompt_j = cJSON_GetObjectItemCaseSensitive(args, "prompt");
    if (!prompt_j || !cJSON_IsString(prompt_j)) {
        cJSON_Delete(args);
        return strdup("Error: 'prompt' is required");
    }

    const char *prompt = prompt_j->valuestring;
    int timeout = 0;

    cJSON *timeout_j = cJSON_GetObjectItemCaseSensitive(args, "timeout");
    if (timeout_j && cJSON_IsNumber(timeout_j)) timeout = timeout_j->valueint;

    /* Write prompt to a temp file to avoid shell quoting issues */
    char tmpfile[512];
    snprintf(tmpfile, sizeof(tmpfile), "/tmp/jb-tool-prompt-%s", g_session_uuid);
    FILE *tf = fopen(tmpfile, "w");
    if (!tf) {
        cJSON_Delete(args);
        return strdup("Error: cannot create temp file for jb tool");
    }
    fwrite(prompt, 1, strlen(prompt), tf);
    fclose(tf);

    /* Build command: cat tempfile | <jb-path> [--config <path>] --parent <uuid> */
    char full_cmd[12288];
    const char *config_arg = g_config_path[0] ? g_config_path : NULL;

    if (timeout > 0) {
        if (config_arg) {
            snprintf(full_cmd, sizeof(full_cmd),
                "timeout %d /bin/sh -c 'cat %s | %s --config %s --parent %s' 2>&1",
                timeout, tmpfile, g_jb_path, config_arg, g_session_uuid);
        } else {
            snprintf(full_cmd, sizeof(full_cmd),
                "timeout %d /bin/sh -c 'cat %s | %s --parent %s' 2>&1",
                timeout, tmpfile, g_jb_path, g_session_uuid);
        }
    } else {
        if (config_arg) {
            snprintf(full_cmd, sizeof(full_cmd),
                "cat %s | %s --config %s --parent %s 2>&1",
                tmpfile, g_jb_path, config_arg, g_session_uuid);
        } else {
            snprintf(full_cmd, sizeof(full_cmd),
                "cat %s | %s --parent %s 2>&1",
                tmpfile, g_jb_path, g_session_uuid);
        }
    }

    FILE *fp = popen(full_cmd, "r");
    if (!fp) {
        unlink(tmpfile);
        cJSON_Delete(args);
        return strdup("Error: cannot execute jb");
    }

    /* Read output with truncation — same logic as tool_bash */
    size_t cap = (size_t)g_max_output_bytes + 1;
    char *output = malloc(cap);
    size_t total = 0;
    char buf[4096];
    size_t nread;
    int truncated = 0;

    while ((nread = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (total + nread >= cap - 1) {
            size_t remaining = cap - total - 1;
            if (remaining > 0) {
                memcpy(output + total, buf, remaining);
                total += remaining;
            }
            truncated = 1;
            while (fread(buf, 1, sizeof(buf), fp) > 0) {}
            break;
        }
        memcpy(output + total, buf, nread);
        total += nread;
    }
    output[total] = '\0';

    int status = pclose(fp);
    unlink(tmpfile);
    cJSON_Delete(args);

    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code != 0) {
            char *result = malloc(total + 128);
            snprintf(result, total + 128, "%s\n[jb child exited with code %d]", output, exit_code);
            free(output);
            return result;
        }
    }

    if (truncated) {
        char *result = malloc(total + 128);
        snprintf(result, total + 128,
                 "%s\n... (output truncated, %ld bytes max)",
                 output, g_max_output_bytes);
        free(output);
        return result;
    }

    return output;
}

char *tool_execute(const char *name, const char *arguments)
{
    if (strcmp(name, "read") == 0)  return tool_read(arguments);
    if (strcmp(name, "write") == 0) return tool_write(arguments);
    if (strcmp(name, "edit") == 0)  return tool_edit(arguments);
    if (strcmp(name, "bash") == 0)  return tool_bash(arguments);
    if (strcmp(name, "jb") == 0)    return tool_jb(arguments);

    char *err = malloc(128);
    snprintf(err, 128, "Error: unknown tool '%s'", name);
    return err;
}
