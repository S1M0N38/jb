# Contrib — jb Listing Tools

Companion shell scripts for browsing jb sessions. All scripts read session
data from `.jb/sessions/<uuid>/` inside the repository (the same layout `jb`
itself uses — see CONTEXT.md / ADR-0006).

## Dependencies

- [jq](https://jqlang.github.io/jq/) — JSON processor (required by all scripts)

## jb-list

List all sessions by reading their `metadata.json` files. The repository is
resolved by walking up from cwd, exactly like `jb` itself.

### Usage

```sh
# Raw JSONL output (one JSON object per line)
jb-list

# Human-readable table
jb-list | jq '.'

# Latest completed session
jb-list | jq -r 'select(.status=="completed") | .uuid' | tail -1

# Sessions matching a subject
jb-list | jq -r 'select(.subject != null) | .subject' | grep -i "README"

# All working/incomplete sessions
jb-list | jq 'select(.status=="working")'

# Sessions sorted by start time (most recent first)
jb-list | jq -s 'sort_by(.started_at) | reverse[]'
```

### Output Format

Each line of output is a raw JSON object — the exact contents of each session's
`metadata.json`. No transformation, sorting, or filtering is applied by the
script itself. Composition is left to `jq`, `grep`, `sort`, etc.

Example output line (keys per the metadata schema — reference §6):

```json
{
  "uuid": "21637177a1b2c3d4e5f60718293a4b5c6",
  "subject": "re-analyze scheduler failures",
  "body": "",
  "author": "32fb4377",
  "parent": null,
  "status": "committed",
  "started_at": "2026-05-25T19:01:53.000Z",
  "ended_at": "2026-05-25T19:02:04.000Z",
  "working_dir": "/Users/simo/Developer/jb",
  "config": {
    "api_url": "https://opencode.ai/zen/go/v1",
    "model": "deepseek-v4-flash",
    "max_tokens": 500000,
    "max_output_lines": 2000,
    "max_output_bytes": 51200
  },
  "turns": 2,
  "tokens_used": 1413,
  "exit_code": 0,
  "last_activity": "2026-05-25T19:02:04.000Z"
}
```

### Programmatic Use

```sh
#!/bin/sh
# Get UUIDs of all committed sessions
committed_uuids=$(jb-list | jq -r 'select(.status=="committed") | .uuid')

for uuid in $committed_uuids; do
    # Process each session
    jb-view "$uuid"
done
```

```python
import subprocess, json

# Parse all sessions into Python objects
result = subprocess.run(["jb-list"], capture_output=True, text=True)
sessions = [json.loads(line) for line in result.stdout.strip().split("\n") if line]

for s in sessions:
    print(f"{s['uuid'][:8]}  {s['status']:12}  {s['subject']}")
```

### Behavior

- Sessions without `metadata.json` are silently skipped.
- Exit code 0 always (even when no sessions exist — outputs nothing).

## jb-view

Render a single session's conversation with ANSI formatting. Reads
`session.jsonl` (pi session format v3) — the authoritative conversation
record.

### Usage

```sh
# View the most recent session
jb-view

# View a specific session by UUID (or unique 4+ hex prefix)
jb-view <uuid>

# Pipe to less for paging
jb-view | less -R

# Combine with jb-list for selection
jb-list | jq -r 'select(.status=="committed") | .uuid' | tail -1 | xargs jb-view
```

### Header Format

When `metadata.json` is present, the session header shows:

```
session fd39d269  committed  deepseek-v4-flash  2 turns
> re-analyze scheduler failures
```

When `metadata.json` is missing, falls back to:

```
session <full-uuid>  2026-05-25 19:01
```

### Message Rendering

| Role       | Prefix   | Style                              |
|------------|----------|------------------------------------|
| system     | *(hidden)* | —                                  |
| user       | `>`      | Cyan bold                          |
| assistant  | `=`      | Green bold                         |
| tool call  | *name*   | Yellow bold + dim summary          |
| tool result| *(varies)* | Dim content (or diff for edit/write) |

Content is truncated to 6 lines per message to keep output readable.

### Programmatic Use

```sh
#!/bin/sh
# Extract just the final answer from a session
jb-view "$uuid" | sed -n '/^=/,$ p' | tail -n +2

# Check if a session completed successfully
jb-list | jq -r "select(.uuid==\"$uuid\") | .status"
```

## Installation

```sh
# Symlink into PATH (optional)
ln -s $(pwd)/contrib/jb-list ~/.local/bin/jb-list
ln -s $(pwd)/contrib/jb-view ~/.local/bin/jb-view
```

Or add `contrib/` to your `$PATH`.
