# test_session.sh — Goal 3: session management

# Run jb (which should create a session) and check side effects
# We need jb to actually create session before exiting
# For now, jb creates a session on startup

# Find the most recent session dir after running jb
_output=$(echo "hello" | "$JB" 2>/dev/null)

# Check that a session directory was created under XDG_CACHE_HOME
_cache_dir="${XDG_CACHE_HOME:-$HOME/.cache}/jb/sessions"
if [ -d "$_cache_dir" ]; then
    pass "sessions directory exists"
else
    fail "sessions directory exists" "not found at $_cache_dir"
fi

# Find the latest session dir
_latest=$(ls -td "$_cache_dir"/*/ 2>/dev/null | head -1)
if [ -n "$_latest" ]; then
    pass "session UUID directory created"

    # Check state.jsonl exists
    assert_file_exists "state.jsonl created" "$_latest/state.jsonl"

    # Check log.jsonl exists
    assert_file_exists "log.jsonl created" "$_latest/log.jsonl"

    # UUID should be 36 chars (32 hex + 4 dashes)
    _uuid=$(basename "$_latest")
    _uuid_len=${#_uuid}
    if [ "$_uuid_len" -eq 36 ]; then
        pass "session UUID is 36 characters"
    else
        fail "session UUID is 36 characters" "got $_uuid_len: $_uuid"
    fi
else
    fail "session UUID directory created" "no dirs in $_cache_dir"
fi
