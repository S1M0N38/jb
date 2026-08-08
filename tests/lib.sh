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

# Per-test session store; set by new_scratch().
JB_SESSIONS_DIR=""

# All scratch dirs created so far; run.sh's EXIT trap removes them
# unless TEST_KEEP=1.
SCRATCHES=""

# new_scratch — fresh scratch dir with an isolated environment, cd into it.
new_scratch() {
    SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/jb-test.XXXXXX")
    SCRATCHES="$SCRATCHES $SCRATCH"
    mkdir -p "$SCRATCH/.config/jb" "$SCRATCH/.cache" "$SCRATCH/tmp"
    if [ -f "$REAL_CFG" ]; then
        cp "$REAL_CFG" "$SCRATCH/.config/jb/config.json"
    else
        echo "jb tests: no config at $REAL_CFG — API tests will fail with exit 3" >&2
    fi
    export HOME="$SCRATCH"
    export XDG_CONFIG_HOME="$SCRATCH/.config"
    export XDG_CACHE_HOME="$SCRATCH/.cache"
    export TMPDIR="$SCRATCH/tmp"
    JB_SESSIONS_DIR="$XDG_CACHE_HOME/jb/sessions"
    cd "$SCRATCH" || exit 1
}

# repo_init — bring up a repository context for a test. Phase 1: the scratch
# dir itself (jb init does not exist yet); phase 2 adds `jb init` here.
repo_init() {
    new_scratch
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
