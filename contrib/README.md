# Contrib — jb Listing Tools

Companion shell scripts for browsing jb sessions. All scripts read session data
from `$XDG_CACHE_HOME/jb/sessions/`.

## Dependencies

- [jq](https://jqlang.github.io/jq/) — JSON processor (required by all scripts)

## jb-list

List all sessions by reading their `metadata.json` files.

### Usage

```sh
# Raw JSONL output (one JSON object per line)
jb-list

# Human-readable table
jb-list | jq '.'

# Latest completed session
jb-list | jq -r 'select(.status=="completed") | .uuid' | tail -1

# Find sessions by title
jb-list | grep -i "README"

# All running/incomplete sessions
jb-list | jq 'select(.status=="running")'

# Sessions sorted by start time (most recent first)
jb-list | jq -s 'sort_by(.started_at) | reverse[]'
```

### Output Format

Each line of output is a raw JSON object — the exact contents of each session's
`metadata.json`. No transformation, sorting, or filtering is applied by the
script itself. Composition is left to `jq`, `grep`, `sort`, etc.

Example output line:

```json
{
  "uuid": "fd39d269-a161-4359-a6c6-78258c63d7ba",
  "status": "completed",
  "title": "Create a file called /tmp/e2e.txt with content...",
  "started_at": "2026-05-25T19:01:53Z",
  "ended_at": "2026-05-25T19:02:04Z",
  "working_dir": "/Users/simo/Developer/jb",
  "model": "glm-5.1",
  "tokens_used": 1413,
  "turns": 2,
  "exit_code": 0
}
```

### Programmatic Use

```sh
#!/bin/sh
# Get UUIDs of all completed sessions
completed_uuids=$(jb-list | jq -r 'select(.status=="completed") | .uuid')

for uuid in $completed_uuids; do
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
    print(f"{s['uuid'][:8]}  {s['status']:12}  {s['title']}")
```

### Behavior

- Sessions without `metadata.json` are silently skipped (backward compatibility
  with older sessions that lack metadata).
- Exit code 0 always (even when no sessions exist — outputs nothing).

## jb-view

Render a single session's conversation with ANSI formatting.

### Usage

```sh
# View the most recent session
jb-view

# View a specific session by UUID
jb-view <uuid>

# Pipe to less for paging
jb-view | less -R

# Combine with jb-list for selection
jb-list | jq -r 'select(.status=="completed") | .uuid' | tail -1 | xargs jb-view
```

### Header Format

When `metadata.json` is present, the session header shows:

```
session fd39d269  completed  glm-5.1  2 turns
> Create a file called /tmp/e2e.txt with content...
```

When `metadata.json` is missing (old sessions), falls back to:

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
#!/bin.sh
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
