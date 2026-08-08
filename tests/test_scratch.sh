# test_scratch.sh — phase 1 slice 1: scratch helpers exist and isolate a run
# The first test to use tests/lib.sh: proves new_scratch redirects all state
# into a scratch dir and that a real run stays inside it.

# Helpers are defined (lib.sh is sourced by run.sh before test files)
for _fn in new_scratch repo_init newest_session session_dir prompt_pong; do
    if command -v "$_fn" >/dev/null 2>&1; then
        pass "helper $_fn is defined"
    else
        fail "helper $_fn is defined" "not found"
    fi
done

# Environment is redirected into the scratch dir; config copied (real provider)
if [ "$HOME" = "$SCRATCH" ] && [ -f "$SCRATCH/.config/jb/config.json" ]; then
    pass "scratch env: HOME redirected and config copied"
else
    fail "scratch env: HOME redirected and config copied" "HOME=$HOME SCRATCH=$SCRATCH"
fi
if [ "$XDG_CACHE_HOME" = "$SCRATCH/.cache" ] && [ "$TMPDIR" = "$SCRATCH/tmp" ]; then
    pass "scratch env: cache and tmp redirected"
else
    fail "scratch env: cache and tmp redirected" "XDG_CACHE_HOME=$XDG_CACHE_HOME TMPDIR=$TMPDIR"
fi

# A real run lands in the repo, never the real cache
repo_init
_out=$(prompt_pong | "$JB" run 2>/dev/null)
_latest=$(newest_session)
if [ -n "$_latest" ]; then
    pass "jb run creates a session in the scratch"
else
    fail "jb run creates a session in the scratch" "no session under $JB_SESSIONS_DIR"
fi
case "$_out" in
    *PONG*) pass "prompt_pong produces a PONG reply" ;;
    *)      fail "prompt_pong produces a PONG reply" "got: $(echo "$_out" | head -c 200)" ;;
esac

# newest_session / session_dir agree on the session location
_uuid=$(basename "$_latest")
if [ -n "$_uuid" ] && [ -f "$(session_dir "$_uuid")/metadata.json" ]; then
    pass "session_dir resolves the session dir"
else
    fail "session_dir resolves the session dir" "$(session_dir "$_uuid")"
fi
