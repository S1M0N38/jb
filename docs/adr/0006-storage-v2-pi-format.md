# Storage v2 — pi format, repo-scoped

Status: accepted (supersedes ADR-0001, ADR-0004, ADR-0005)

## Decision

jb's conversation storage moves from the XDG-cache wire-format layout to a
**repository-scoped** layout: every session lives at
`.jb/sessions/<uuid>/` inside the project, and the conversation is recorded
in the **pi session format** (JSONL v3) rather than a jb-native message
log.

```
.jb/
  config.json                   // LOCAL config (git config --local analog)
  sessions/<uuid>/
    metadata.json               // jb index: lifecycle, subject/body, lineage, tokens
    session.jsonl               // authoritative conversation — pi session format v3
    events.jsonl                // live stream — pi json-mode events, delta-only
```

The three files replace the previous `~/.cache/jb/sessions/<uuid>/` layout
with `state.jsonl` (wire-format message log) and `log.jsonl` (raw SSE log).
`state.jsonl` and `log.jsonl` are **deleted**; there is no migration — old
sessions are not readable.

## Why

Two problems with the old layout:

1. **Cache directories are shared state.** `$XDG_CACHE_HOME/jb/sessions/`
   is machine-global, not project-scoped: sessions from unrelated projects
   interleave, cleanup is manual, and a session's "working directory" is a
   metadata field rather than a structural fact. Git solved this a long
   time ago — the repository walks up from cwd, and everything lives under
   `.git/`. jb borrows that shape: `.jb/` is a plain directory, `jb init`
   creates it, and any verb outside a repository is a fatal error.

2. **The wire format is a private dialect.** jb's message log used the
   OpenAI wire shape, which means jb sessions could only be consumed by
   jb. The pi coding agent stores sessions as JSONL v3 — a stable,
   documented interchange format with viewers, exporters, and a browser
   renderer (pi.dev) that already exist. Storing the conversation in the
   pi format makes every jb session renderable by pi's tooling unchanged,
   and `jb export` embeds the pi viewer template in the binary.

## Consequences

- The OpenAI wire shape is **derived** from the pi-format message array at
  request time (`build_request_body()`) and never stored.
- `session.jsonl` is the only conversation record: jb appends to it and
  reads it back (for `--fork` / `--seed` / `$JB_SESSION` continuation),
  trimming any dangling tail from interrupted sessions.
- `events.jsonl` is the live stream in pi json-mode (delta-only, 0.84
  wire protocol) — any tool that reads `pi --mode json` can consume it.
- `metadata.json` becomes the jb-native index: lifecycle (working →
  completed → committed), subject/body, author/parent lineage, config
  snapshot, token/turn counts. Written atomically at init and close;
  rewritten by `jb commit` — which never appends to `session.jsonl`, so
  forks never inherit the summary turn.
- Lineage changes shape: the old `--parent` flag (ADR-0005) is replaced by
  `--fork`/`--seed` and the `$JB_SESSION` environment variable; a fork's
  header carries `parentSession`, and its metadata records `parent`.
- Repo-scoped storage makes tests self-contained: every test runs in its
  own scratch directory with its own `.jb/` — no shared cache, no
  cross-test interference (implementation plan phase 1).
