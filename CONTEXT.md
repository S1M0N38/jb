# jb

A git-inspired agentic coding loop — single C binary, zero runtime deps except `curl` and POSIX. Cloud + local API client. Written in C, targets low-end hardware. Sessions are stored in the pi session format (JSONL v3), so pi's viewers and exporters render jb sessions unchanged. Storage: `.jb/sessions/<uuid>/` — repo-scoped, per ADR-0006.

## Language

**Turn**: One cycle of the agentic loop — send request to API, receive response, execute any tool calls, send results back. A turn ends when the model emits a final text message (`stopReason: "stop"`).

**Session**: The full conversation maintained by jb as an in-memory message array in the pi format. Stored on disk in `session.jsonl` (pi session format v3). Resent with every API call — the API is stateless. Lives at `.jb/sessions/<uuid>/` inside the repository.

**Commit**: The deliberate act that finalizes a session. A session runs (`working`), finishes (`completed`), and only an explicit `jb commit` finalizes it (`committed`). A commit rewrites `metadata.json` only — `session.jsonl` is never touched, so forks never inherit the summary turn.

**Fork**: A new session started from a previous one via `jb run --fork ID`. The fork loads the parent's `session.jsonl`, converts it to wire messages, trims any dangling tail (an interrupted source session never leaks unpaired tool calls), and rebuilds the system prompt fresh. The fork's header carries `parentSession`; its metadata records `parent`.

**Session log / events**: `events.jsonl`, the live stream — pi json-mode events, delta-only (`agent_start`, `message_start/update/end`, `tool_execution_start/end`, `agent_end`). `tail -f` is the UI. Any tool that reads `pi --mode json` can consume this file.

**Tool**: A function the model can invoke during a turn. jb provides five: `read`, `write`, `edit`, `bash`, `jb`.

**Skill**: A markdown file that becomes part of the system prompt. Markdown-as-program.

**Provider**: Any OpenAI Chat Completions-compatible API endpoint. Cloud (OpenAI, DeepSeek, etc.) or local (Ollama, llama.cpp, etc.).

**Session metadata**: A `metadata.json` written into each session directory at init (status `working`, subject from prompt, started_at, working_dir, config snapshot, parent/author) and at close (final status, ended_at, turns, tokens_used, exit_code). Rewritten by `jb commit` (status `committed`, subject/body). Heartbeat: rewritten every turn while working (`last_activity`). The `author` is the creator: `@` (the spawning session) or `--seed` provenance; `""` = human. `parent` is set only by `--fork`. Atomic writes (temp file + rename). Enables tools to list and search sessions without parsing `session.jsonl`.

**Session lister**: A sidecar shell script (`contrib/jb-list`) that scans `.jb/sessions/*/metadata.json` and outputs their contents as JSONL (one object per line). Composable via pipes and jq. jb knows nothing about it.

**ID resolution**: Everywhere a session ID is accepted — `full uuid | unique 4+ hex prefix | @`. `@` = `$JB_SESSION`.

_Avoid_: Plugin, extension, module, GUI, TUI (jb has none of these)

## Streams

- **stdout** — the model's answer, streamed as it arrives (for `jb run`); command output for the metadata verbs.
- **stderr** — the session banner (`jb: session <id> started ...`), diagnostics, fatal errors.
- **session.jsonl** — the authoritative conversation: header line + one entry per message, each with unique 8-hex id, parentId chain, ISO-ms timestamp, role, content blocks, usage, stopReason. Append-only.
- **events.jsonl** — the live stream: pi json-mode, delta-only. First line is the session header (same object as session.jsonl's).
- **metadata.json** — the jb index (see Language above).
- **Temp files** — bash output exceeding `max_output_bytes` saved to `$TMPDIR/jb-<uuid>-bash-<N>.out` (respects `$TMPDIR`, defaults to `/tmp`). Path included in tool result.
- **Exit codes**: 0 = success · 1 = error/not found/refused · 2 = usage.
- **Error retries**: network/429/5xx retried with backoff. Auth/400 errors exit immediately. Malformed tool arguments sent back to model as error for self-correction.
- **Signal handling**: SIGPIPE = die (default). SIGINT/SIGTERM = kill child processes, record the assistant entry with `stopReason: "aborted"`, close the session with status error.
- **No file locking**: concurrent jb processes are independent. Metadata writes are atomic; the files are the record of truth.

## Relationships

- A **Session** consists of multiple **Turns**, recorded in `session.jsonl`
- Each **Turn** may produce zero or more **Tool** calls, which are executed and fed back
- A **Skill** is injected into the system prompt (index only), the model reads the full SKILL.md via `read` when needed
- A **Provider** serves all **Sessions** — any chat/completions endpoint works
- A **Fork** is a session whose history starts from a parent session's recorded conversation (`--fork` / `$JB_SESSION` lineage)
- A **Sub-agent** is a child `jb` process spawned via the `jb` tool. It has its own session, reads the same working directory, returns its final answer via stdout. The child's `author` is the parent's session id (`@`) — the model does not see it.
- The **session tree** is an observability artifact — sessions linked by `parent`/`author` in metadata. Roots are sessions with no parent. Reconstructed from metadata by `jb-list` and jq. No runtime coordination.

## System prompt

Hardcoded base prompt (~20 lines): who jb is, what the five tools do, be concise, execute, current date (`YYYY-MM-DD`), current working directory. Sent as `role: "system"` message. Then appended:
- AGENTS.md content (walked from cwd up)
- Skills index (XML-style `<available_skills>` block with name, description, full file path per skill)

The system prompt is rebuilt fresh for every run — **never persisted**, and forks do not inherit the parent's prompt snapshot. It lives only in the HTML export payload (`jb export`).

## Storage

```
.jb/                            // created by 'jb init' — a plain directory
  config.json                   // LOCAL config (git config --local analog)
  sessions/<uuid>/              // one directory per session
    metadata.json               // jb index: lifecycle, subject/body, lineage, tokens
    session.jsonl               // authoritative conversation — pi session format v3
    events.jsonl                // live stream — pi json-mode events, delta-only
```

- `state.jsonl` / `log.jsonl` — **deleted**. No migration from `~/.cache/jb/sessions/`; old sessions are not readable (ADR-0006).
- Repo resolution: walk up from cwd looking for `.jb/`; outside any repo → `jb: fatal: not a jb repository (run 'jb init')`, exit 1. `-C DIR` resolves from DIR.
- All metadata writes are atomic (temp file + rename). Concurrent readers never see a partial file.
- `events.jsonl` is derived data, grows unbounded — cleanup is the filesystem.

### session.jsonl (pi session format v3)

Line 1 is the header; every following line is an entry. This is the **only** conversation record; jb appends to it and reads it back.

```
{"type":"session","version":3,"id":"<uuid>","timestamp":"<ISO-ms>","cwd":"<working_dir>"}
{"type":"message","id":"a1b2c3d4","parentId":null,"timestamp":"<ISO-ms>","message":{"role":"user","content":[{"type":"text","text":"…"}],"timestamp":<epoch-ms>}}
{"type":"message","id":"e5f60718","parentId":"a1b2c3d4","timestamp":"<ISO-ms>","message":{"role":"assistant","content":[{"type":"text","text":"…"},{"type":"toolCall","id":"call_1","name":"read","arguments":{"path":"src/jb.c"}}],"api":"openai-completions","provider":"<host>","model":"<cfg model>","usage":{…},"stopReason":"stop|toolUse|error|aborted","timestamp":<epoch-ms>}}
{"type":"message","id":"…","parentId":"e5f60718","timestamp":"<ISO-ms>","message":{"role":"toolResult","toolCallId":"call_1","toolName":"read","content":[{"type":"text","text":"…"}],"isError":false,"timestamp":<epoch-ms>}}
```

With a parent (fork), the header adds `"parentSession":"/abs/path/.jb/sessions/<parent-uuid>/session.jsonl"`.

Field rules:
- `id`: 8 hex chars from `/dev/urandom`, collision-checked against ids already in the session.
- `parentId`: id of the previous entry; `null` for the first. jb sessions are single-branch — a linear chain (pi's branch-export shape).
- `timestamp`: ISO 8601 UTC with milliseconds (`2026-08-06T00:33:50.332Z`). `message.timestamp`: epoch ms — distinct from the entry timestamp; pi writes both, jb emits both.
- `toolCall.arguments`: parsed JSON object, not string. Unparseable → `{}`.
- `toolName`: the tool name from the assistant's matching `toolCall`.
- `isError`: `true` when the tool result is an error (exit code ≠ 0, or result starts with `Error:`).
- `stopReason`: `"toolUse"` when finish_reason == tool_calls; `"stop"` otherwise; `"error"`/`"aborted"` with `errorMessage` on API failure / signal paths.
- `usage`: from the SSE `usage` chunk (input = prompt_tokens, output = completion_tokens, reasoning = reasoning_tokens or 0, totalTokens = total_tokens; cacheRead/cacheWrite 0; cost all zeros — jb has no pricing).
- `api`/`provider`: `api:"openai-completions"` always (the wire dialect); `provider` = hostname of api_url (api.openai.com→openai, localhost:11434→ollama, else the host).
- jb does NOT write `session_info`, `model_change`, `thinking_level_change`, `compaction`, `branch_summary`, `custom`, `label` — pi's loader tolerates their absence.

### events.jsonl (the live stream)

Format = pi `--mode json` with the 0.84 delta-only wire protocol: `message_update` carries **no cumulative `message`** and **no `partial`** — consumers assemble from `contentIndex` + `delta`; `message_end` is authoritative. Event types jb emits: `session` (header), `agent_start`, `message_start`, `message_update` (text_delta / thinking_delta / toolcall_start / toolcall_delta / toolcall_end), `message_end`, `tool_execution_start`, `tool_execution_end`, `agent_end`. No `turn_*`/`queue_*`/`compaction_*` events (jb has no steering, follow-ups, or compaction).

## API contract (OpenAI Chat Completions)

The in-memory message array **IS** the pi format (§session.jsonl above). The OpenAI wire JSON is built from it inside `build_request_body()` — nowhere else:

| pi (in-memory / session.jsonl) | OpenAI wire |
|---|---|
| `user` content blocks | `{"role":"user","content":"<joined text>"}` |
| `assistant` text + toolCall blocks | `{"role":"assistant","content":"<joined text>" (or null),"tool_calls":[{"id":"…","type":"function","function":{"name":"…","arguments":"<unformatted JSON>"}}]}` |
| `toolResult` | `{"role":"tool","tool_call_id":"…","content":"<joined text>"}` |

System prompt: `{"role":"system","content":prompt_build()}` prepended at request time. `tools` (the five tool definitions) sent every turn. `stream: true`, `stream_options: {"include_usage": true}`.

**SSE streaming** — `data:` lines from the response:
- `choices[0].delta.content` — text deltas, accumulate for the final answer
- `choices[0].delta.tool_calls[i]` — tool call deltas, accumulate `id`, `function.name`, `function.arguments` across chunks
- `choices[0].finish_reason === "tool_calls"` — model wants to call tools, execute them and loop
- `choices[0].finish_reason === "stop"` — model is done, print final answer to stdout
- `usage` — token counts for the session record and budget tracking
- `data: [DONE]` — stream complete

**Load path** (`--fork` / `--seed` / `$JB_SESSION` continuation): read `.jb/sessions/<uuid>/session.jsonl`, validate the v3 header, convert entries → wire messages, trim the dangling tail (drop entries after the last complete assistant message — stopReason ∈ {stop, toolUse} with all its toolResults present).

## Example dialogue

> **Dev:** "When jb uses the `jb` tool, what happens?"
> **Domain expert:** "A new jb process starts as a subprocess. The parent's session uuid is exported as `JB_SESSION`, so the child records the parent as its `author` in metadata. The parent sees the child's stdout as the tool result. The child has its own session, its own provider call."

> **Dev:** "How does the conversation survive across turns?"
> **Domain expert:** "The in-memory message array is in the pi format; every append is written to `session.jsonl`. Each turn, the array is converted to the OpenAI wire shape and the whole thing is sent to the API. The API is stateless."

> **Dev:** "What happens when I `jb commit` a session?"
> **Domain expert:** "Metadata only. The session must be completed or error; the parent (if any) must be committed. The subject/body come from `-m` or are auto-generated by the model. `session.jsonl` is byte-identical before and after — forks never inherit the summary turn."

> **Dev:** "Can jb work with a local model?"
> **Domain expert:** "Yes — any endpoint that speaks chat/completions works. Point `api_url` at `http://localhost:11434/v1` for Ollama."

## Skills and Context

**AGENTS.md**: A markdown file loaded unconditionally into the system prompt. Discovered by walking from cwd upward. Always-on project context.

**Skill**: A directory containing `SKILL.md` with YAML frontmatter (`name`, `description`) and a markdown body. Loaded on-demand by the model via the `read` tool.

**Skills index**: An XML-style block injected into the system prompt at startup. Each skill lists its name, description, and full file path. The model reads the full `SKILL.md` with the `read` tool when it decides a skill is relevant.

**YAML frontmatter**: Only `name` and `description` are extracted. Simple key-value parsing within `---` delimiters. No YAML library.

_Avoid_: Plugin, extension, module

## Discovery paths

- `AGENTS.md` — walked from cwd up to root, concatenated
- `$XDG_CONFIG_HOME/jb/agents/skills/*/SKILL.md` — global skills
- `.agents/skills/*/SKILL.md` — project-local skills
- `.gitignore`, `.ignore`, `.fdignore` — respected during skill directory scanning

## Invocation

jb is a git-inspired CLI. The repository is resolved by walking up from cwd; `-C DIR` overrides the starting point.

```bash
jb init                                  # create .jb/ (idempotent)
echo "fix the bug in main.c" | jb run    # the agentic loop
echo "continue" | jb run --fork 9f3c2a1b # fork a previous session
jb commit 9f3c2a1b -m "fix the bug"      # finalize (metadata only)
jb status                                # current session + repo summary
jb log --graph                           # the committed forest
jb ps                                    # children of the current session
jb wait 9f3c2a1b                         # poll until completed/error
jb path 9f3c2a1b                         # print the session directory
jb export 9f3c2a1b /tmp/s.html           # pi viewer HTML
jb config model gpt-4.1                  # git-style config get/set
jb help [VERB]                           # the command reference
```

**Global flags**:
- `-C DIR` — resolve the repository from DIR (git -C analog)
- `-c KEY=VALUE` — config override (repeatable; meaningful for run)
- `--config <path>` — load config from PATH instead of the global file
- `--version` — print version string to stdout, exit 0
- `--help` — print the command reference, exit 0
- bare `jb` — `jb help`

**Environment**:
- `JB_SESSION` — the running session's uuid; set by `jb run`, inherited by popen children; resolved as `@` by the metadata verbs. Stale env → `jb: JB_SESSION <id> not found`, exit 1.
- `JB_API_KEY` / `OPENAI_API_KEY` — API key.
- `XDG_CONFIG_HOME` — global config location (default `~/.config`).

**Version**: Hardcoded as `#define JB_VERSION "x.y"` in `src/version.h`. Bumped manually on release. Printed by `jb --version` as `jb x.y`.

## Configuration

Git-style, two files, local merged over global; per-run `-c` overrides:

- **Global**: `~/.config/jb/config.json` (or `$XDG_CONFIG_HOME/jb/config.json`)
- **Local**: `.jb/config.json`
- **Per-run**: `jb -c max_tokens=10000 run` (repeatable)

```json
{
  "api_url": "https://api.openai.com/v1",
  "model": "gpt-4.1",
  "max_tokens": 500000,
  "max_output_lines": 2000,
  "max_output_bytes": 51200
}
```

All fields are optional. Any key is accepted and stored as a string, coerced at use. The effective configuration is snapshotted into each session's `metadata.json` at run time — the replication record.

## Documentation

**Man page**: The reference documentation for jb. Installed as `man jb`. Written in standard nroff (`-man` macros). Serves two audiences: humans at a terminal, and jb itself (via `man jb | col -b` through the bash tool). Not a spec — the source code is the source of truth for tool behavior. The man page provides context, conventions, and guidance.

**Self-reference hint**: The system prompt includes one line — "To read your own documentation, run `man jb | col -b`." — so the model knows it can access its own docs. No cost unless the model decides to read the man page.

**ADRs**: `docs/adr/` records architectural decisions. ADR-0006 (storage v2 — pi format, repo-scoped) supersedes ADRs 0001, 0004, and 0005.

_Avoid_: README, help flag, --docs, embedded documentation

## Flagged ambiguities

- "agent" was used to mean both the jb binary and the LLM — resolved: jb is the **agent**, the LLM is the **model**.
- "state" was used to mean both `state.jsonl` (deleted) and the in-memory message array — resolved: the file is `session.jsonl`, the array is "the message array".
