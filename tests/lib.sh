# tests/lib.sh — scratch-dir isolation helpers for the jb test suite
# Sourced by tests/run.sh before any test file. Every test_*.sh runs inside
# its own scratch dir (new_scratch, called by run.sh) with HOME,
# XDG_CONFIG_HOME, XDG_CACHE_HOME and TMPDIR redirected into it, so no test
# can touch the real ~/.cache, ~/.config, the repo, or /tmp. The real jb
# config is copied into the scratch so tests keep hitting the real provider
# (api_url/model unchanged). Isolation rules — implementation plan phase 1:
#   1. every test runs in its own scratch dir
#   2. HOME/XDG_CONFIG_HOME overridden into the scratch dir
#   3. scratch dirs removed on exit (trap in run.sh) unless TEST_KEEP=1
#   4. no test writes outside its scratch dir except the mock-free API calls
#   5. optional tests are skipped when `command -v pi` fails

# Captured at source time — BEFORE any scratch override, so this is the
# user's real config location.
REAL_CFG="${XDG_CONFIG_HOME:-$HOME/.config}/jb/config.json"

# Per-test session store; set by repo_init() to $SCRATCH/.jb/sessions.
JB_SESSIONS_DIR=""

# Captured at source time — BEFORE any scratch override: the real tmp root.
# Scratch dirs must be SIBLINGS under it: new_scratch() overrides TMPDIR into
# the scratch, so re-reading ${TMPDIR:-/tmp} per call would nest scratches
# and leak repo context between tests (repo walk-up finds an outer .jb).
REAL_TMP="${TMPDIR:-/tmp}"

# All scratch dirs created so far; run.sh's EXIT trap removes them
# unless TEST_KEEP=1.
SCRATCHES=""

# new_scratch — fresh scratch dir with an isolated environment, cd into it.
new_scratch() {
    SCRATCH=$(mktemp -d "$REAL_TMP/jb-test.XXXXXX")
    SCRATCHES="$SCRATCHES $SCRATCH"
    mkdir -p "$SCRATCH/.config/jb" "$SCRATCH/.cache" "$SCRATCH/tmp"
    if [ -f "$REAL_CFG" ]; then
        cp "$REAL_CFG" "$SCRATCH/.config/jb/config.json"
    else
        echo "jb tests: no config at $REAL_CFG — API tests will fail (exit 1)" >&2
    fi
    export HOME="$SCRATCH"
    export XDG_CONFIG_HOME="$SCRATCH/.config"
    export XDG_CACHE_HOME="$SCRATCH/.cache"
    export TMPDIR="$SCRATCH/tmp"
    JB_SESSIONS_DIR="$XDG_CACHE_HOME/jb/sessions"
    cd "$SCRATCH" || exit 1
}

# repo_init — bring up a repository context for a test: jb init in the
# current scratch dir (phase 2: jb init exists now).
repo_init() {
    "$JB" init >/dev/null 2>&1 || return 1
    JB_SESSIONS_DIR="$SCRATCH/.jb/sessions"
    cd "$SCRATCH" || exit 1
}

# newest_session — path (trailing slash) of the most recently started session
# dir, or empty when the scratch has no sessions yet.
newest_session() {
    ls -td "$JB_SESSIONS_DIR"/*/ 2>/dev/null | head -1
}

# session_dir <uuid> — path of the session dir for a full uuid.
session_dir() {
    echo "$JB_SESSIONS_DIR/$1"
}

# prompt_pong — the canonical stable prompt. PONG-style prompts are reliable
# in practice with real providers (implementation plan §11), so tests use
# this one prompt for their API calls.
prompt_pong() {
    echo "reply with exactly the word PONG"
}

# iso_ms <epoch-seconds> — ISO-8601 UTC with milliseconds, e.g.
# 2026-08-06T00:33:50.000Z. Portable across BSD (date -r) and GNU (date -d).
iso_ms() {
    if date -u -r "$1" +%Y-%m-%dT%H:%M:%S.000Z 2>/dev/null; then
        :
    else
        date -u -d "@$1" +%Y-%m-%dT%H:%M:%S.000Z
    fi
}

# fixture_session <uuid> <status> [subject] [author] [parent] [start_ago]
# [exit_code] — hand-built session dir + metadata.json for the metadata
# verbs (phase 6: fixture repos, no API calls). Subject defaults to the
# uuid; started_at is exactly 2 minutes ago (or start_ago seconds) and
# ended_at now (unless status is working — then no ended_at). The fixture
# mirrors what session.c writes (§6 of the reference).
fixture_session() {
    _fs_uuid="$1"; _fs_status="$2"
    _fs_subject="${3:-fixture $1}"
    _fs_author="${4:-}"
    _fs_parent="${5:-}"
    _fs_start_ago="${6:-120}"
    _fs_exit="${7:-0}"
    _fs_dir="$JB_SESSIONS_DIR/$_fs_uuid"
    mkdir -p "$_fs_dir"
    printf '{"type":"session","version":3,"id":"%s"}\n' "$_fs_uuid" > "$_fs_dir/session.jsonl"
    : > "$_fs_dir/events.jsonl"
    _fs_now=$(date +%s)
    _fs_start=$(iso_ms $((_fs_now - _fs_start_ago)))
    _fs_end=$(iso_ms "$_fs_now")
    {
        printf '{\n'
        printf '  "uuid": "%s",\n' "$_fs_uuid"
        printf '  "subject": "%s",\n' "$_fs_subject"
        printf '  "body": "",\n'
        printf '  "author": "%s",\n' "$_fs_author"
        if [ -n "$_fs_parent" ]; then
            printf '  "parent": "%s",\n' "$_fs_parent"
        fi
        printf '  "status": "%s",\n' "$_fs_status"
        printf '  "started_at": "%s",\n' "$_fs_start"
        if [ "$_fs_status" != "working" ]; then
            printf '  "ended_at": "%s",\n' "$_fs_end"
        fi
        printf '  "working_dir": "%s",\n' "$SCRATCH"
    printf '  "config": {"api_url":"%s","model":"%s"},\n' \
        "$(sed -n 's/.*"api_url"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$SCRATCH/.config/jb/config.json" 2>/dev/null | head -1)" \
        "$(sed -n 's/.*"model"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$SCRATCH/.config/jb/config.json" 2>/dev/null | head -1)"
        printf '  "turns": 1, "tokens_used": 10, "exit_code": %s,\n' "$_fs_exit"
        printf '  "last_activity": "%s"\n' "$_fs_end"
        printf '}\n'
    } > "$_fs_dir/metadata.json"
}
