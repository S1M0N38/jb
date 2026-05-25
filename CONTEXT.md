# jb

A minimal agentic coding loop — single C binary, zero runtime deps except `curl` and POSIX. Cloud + local API client. Written in C, targets low-end hardware.

## Language

**Turn**: One cycle of the agentic loop — send request to API, receive response, execute any tool calls, send results back. A turn ends when the model emits a final text message (`finish_reason: "stop"`).

**Session**: The full conversation maintained by jb as a message array. Stored on disk as JSONL. Resent with every API call — the API is stateless.

**Session log**: A file written in real-time from raw SSE events. The primary observability surface — use `tail -f` to watch the agent work.

**Tool**: A function the model can invoke during a turn. jb provides four: `read`, `write`, `edit`, `bash`.

**Skill**: A markdown file that becomes part of the system prompt. Markdown-as-program.

**Provider**: Any OpenAI Chat Completions-compatible API endpoint. Cloud (OpenAI, DeepSeek, etc.) or local (Ollama, llama.cpp, etc.).

_Avoid_: Plugin, extension, module (jb has none of these)

## Streams

- **stdout** — the model's final text message only. Written once after the turn completes. Never partial. Clean pipe citizen.
- **stderr** — silent always (Rule of Silence). All diagnostics go to the session log.
- **Session log** — raw SSE events written in real-time to `$XDG_CACHE_HOME/jb/sessions/<uuid>/log.jsonl`. Full transcript. `tail -f` is the UI. A future viewer can parse and present the events.
- **Session state** — append-only JSONL at `$XDG_CACHE_HOME/jb/sessions/<uuid>/state.jsonl` — one JSON object per line, each representing a message. The full conversation. Rebuilt on startup by reading all lines.
- **Temp files** — bash output exceeding `max_output_bytes` saved to `$TMPDIR/jb-<uuid>-bash-<N>.out` (respects `$TMPDIR`, defaults to `/tmp`). Path included in tool result.
- **Loop termination**: configurable `max_tokens` budget (total tokens across all turns). When exhausted, prints partial results and exits code 2.
- **Bash timeout**: optional parameter on the bash tool. The model decides the timeout per command. No cap. If the model omits it, the command runs until it finishes or the user kills the process.
- **Exit codes**: 0 = success, 1 = API/model error, 2 = token budget exhausted or tool error, 3 = configuration error, 130 = SIGINT, 143 = SIGTERM.
- **Error retries**: network/429/5xx retried 3x with exponential backoff (2s base). Auth/400 errors exit immediately. Malformed tool arguments sent back to model as error for self-correction.
- **Signal handling**: SIGPIPE = die (default). SIGINT/SIGTERM = kill child processes, write final state to log, print partial answer to stdout, exit with 130/143.
- **No file locking**: concurrent jb processes are independent. Race conditions on shared files are the model's problem (use git).

## Relationships

- A **Session** consists of multiple **Turns**, tracked in the local state file
- Each **Turn** may produce zero or more **Tool** calls, which are executed and fed back
- A **Skill** is injected into the system prompt (index only), the model reads the full SKILL.md via `read` when needed
- A **Provider** serves all **Sessions** — any chat/completions endpoint works
- A **Sub-agent** is a child `jb` process spawned via `bash`. It has its own session, reads the same working directory, returns its final answer via stdout.

## System prompt

Hardcoded base prompt (~20 lines): who jb is, what the four tools do, be concise, execute, mention sub-agent capability, current date (`YYYY-MM-DD`), current working directory. Sent as `role: "system"` message. Then appended:
- AGENTS.md content (walked from cwd up)
- Skills index (XML-style `<available_skills>` block with name, description, full file path per skill)

## API contract (OpenAI Chat Completions)

**Every turn** — sent to `POST /v1/chat/completions`:

```json
{
  "model": "<from config>",
  "messages": [
    {"role": "system", "content": "<system prompt + AGENTS.md + skills index>"},
    {"role": "user", "content": "<stdin>"},
    {"role": "assistant", "content": null, "tool_calls": [
      {"id": "call_x", "type": "function", "function": {"name": "read", "arguments": "{\"path\":\"main.c\"}"}}
    ]},
    {"role": "tool", "tool_call_id": "call_x", "content": "<file contents>"},
    {"role": "assistant", "content": "I found the bug..."}
  ],
  "tools": [
    {"type": "function", "function": {"name": "read", "description": "...", "parameters": {...}}},
    {"type": "function", "function": {"name": "write", "description": "...", "parameters": {...}}},
    {"type": "function", "function": {"name": "edit", "description": "...", "parameters": {...}}},
    {"type": "function", "function": {"name": "bash", "description": "...", "parameters": {...}}}
  ],
  "stream": true,
  "stream_options": {"include_usage": true}
}
```

The full `messages` array is sent every turn. `tools` is sent every turn. The API is stateless.

**SSE streaming** — `data:` lines from the response:

- `choices[0].delta.content` — text deltas, accumulate for final answer
- `choices[0].delta.tool_calls[i]` — tool call deltas, accumulate `id`, `function.name`, `function.arguments` across chunks
- `choices[0].finish_reason === "tool_calls"` — model wants to call tools, execute them and loop
- `choices[0].finish_reason === "stop"` — model is done, print final answer to stdout
- `usage` — token counts for budget tracking
- `data: [DONE]` — stream complete

**Tool result format** — appended as `role: "tool"` message:
```json
{"role": "tool", "tool_call_id": "call_x", "content": "<result>"}
```

**Assistant tool call** — stored in state as:
```json
{"role": "assistant", "content": null, "tool_calls": [{"id": "call_x", "type": "function", "function": {"name": "read", "arguments": "{\"path\":\"main.c\"}"}}]}
```

## Example dialogue

> **Dev:** "When jb runs `bash` and the command is `jb`, what happens?"
> **Domain expert:** "A new jb process starts — it's just a subprocess. The parent jb sees the child's stdout as the tool result. The child jb has its own session, its own provider call."

> **Dev:** "Does jb keep the full conversation in memory?"
> **Domain expert:** "Yes — the full message array is stored in `state.jsonl` on disk. Every turn, jb reads it, appends the new messages, and sends the whole array to the API. The API is stateless."

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

jb is a filter. Reads prompt from stdin, prints answer to stdout. Behavior is fully determined by config.json + environment + working directory + filesystem.

```bash
echo "fix the bug in main.c" | jb
jb <<< "refactor auth.c"
cat spec.md | jb
{ echo "Debug this:"; cat backtrace.log; } | jb   # parent jb spawning child
```

**Flags**:
- `jb --version` — print version string to stdout, exit 0
- `jb --help` — print usage hint pointing to `man jb`, exit 0
- `jb` (no flags, stdin is a tty) — print usage hint to stderr, exit 3
- `jb` (no flags, stdin has data) — normal operation

These are the only flags. No other arguments are recognized. Any other token on the command line is silently ignored (the prompt always comes from stdin).

**Version**: Hardcoded as `#define JB_VERSION "x.y"` in a header. Bumped manually on release. Printed by `jb --version` as `jb x.y`.

## Configuration

- **Config file**: `$XDG_CONFIG_HOME/jb/config.json`
- **API key**: `JB_API_KEY` or `OPENAI_API_KEY` environment variable
- **No flags, no arguments, no CLI options**

```json
{
  "api_url": "https://api.openai.com/v1",
  "model": "gpt-4.1",
  "max_tokens": 500000,
  "max_output_lines": 2000,
  "max_output_bytes": 51200
}
```

## Documentation

**Man page**: The reference documentation for jb. Installed as `man jb`. Written in standard nroff (`-man` macros). Serves two audiences: humans at a terminal, and jb itself (via `man jb | col -b` through the bash tool). Not a spec — the source code is the source of truth for tool behavior. The man page provides context, conventions, and guidance.

**Self-reference hint**: The system prompt includes one line — "To read your own documentation, run `man jb | col -b`." — so the model knows it can access its own docs. No cost unless the model decides to read the man page.

_Avoid_: README, help flag, --docs, embedded documentation

## Flagged ambiguities

- "agent" was used to mean both the jb binary and the LLM — resolved: jb is the **agent**, the LLM is the **model**.
