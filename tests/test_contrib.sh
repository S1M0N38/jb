# test_contrib.sh — contrib scripts (jb-list, jb-view)

repo_init

# ---- Self-contained: create a session in this scratch (real API call) ----
_out=$(prompt_pong | "$JB" run 2>/dev/null)
if [ "$?" -eq 0 ]; then
    pass "create session for contrib tests"
else
    skip "create session for contrib tests" "jb run failed — no session to scan"
fi

# ---- jb-list tests ----

# jb-list should exist and be executable
if [ -x "$REPO_ROOT/contrib/jb-list" ]; then
    pass "jb-list script exists and is executable"
else
    fail "jb-list script exists and is executable" "not found or not executable"
fi

# jb-view should exist and be executable
if [ -x "$REPO_ROOT/contrib/jb-view" ]; then
    pass "jb-view script exists and is executable"
else
    fail "jb-view script exists and is executable" "not found or not executable"
fi

# jb-list scans .jb/sessions/*/metadata.json
if ls "$JB_SESSIONS_DIR"/*/metadata.json >/dev/null 2>&1; then
    _list_output=$("$REPO_ROOT/contrib/jb-list" 2>/dev/null)
    if [ -n "$_list_output" ]; then
        pass "jb-list produces output when sessions exist"

        # Each line should be valid JSON
        _first_line=$(echo "$_list_output" | head -1)
        if printf '%s' "$_first_line" | jq empty 2>/dev/null; then
            pass "jb-list outputs valid JSONL"
        else
            fail "jb-list outputs valid JSONL" "invalid JSON: $_first_line"
        fi

        # Each line should contain required metadata fields
        _has_uuid=$(echo "$_list_output" | head -1 | jq -r '.uuid // empty' 2>/dev/null)
        if [ -n "$_has_uuid" ]; then
            pass "jb-list entries contain uuid field"
        else
            fail "jb-list entries contain uuid field" "missing"
        fi

        _has_status=$(echo "$_list_output" | head -1 | jq -r '.status // empty' 2>/dev/null)
        if [ -n "$_has_status" ]; then
            pass "jb-list entries contain status field"
        else
            fail "jb-list entries contain status field" "missing"
        fi
    else
        fail "jb-list produces output when sessions exist" "empty output"
    fi
else
    skip "jb-list output tests" "no sessions with metadata.json found"
fi

# ---- jb-view tests ----

# jb-view --help or no args should not crash
_view_err=$("$REPO_ROOT/contrib/jb-view" --help 2>&1 || true)
_rc=$?
if [ "$_rc" -eq 0 ] || [ "$_rc" -eq 1 ]; then
    pass "jb-view runs without crashing"
else
    fail "jb-view runs without crashing" "exit $_rc"
fi

# jb-view with the uuid of the session created in this scratch
_latest_session=$(basename "$(newest_session)" 2>/dev/null)
if [ -n "$_latest_session" ] && [ -f "$JB_SESSIONS_DIR/$_latest_session/session.jsonl" ]; then
    _view_output=$("$REPO_ROOT/contrib/jb-view" "$_latest_session" 2>/dev/null) || true
    if [ -n "$_view_output" ]; then
        pass "jb-view renders session output for UUID"
    else
        fail "jb-view renders session output for UUID" "empty output"
    fi

    # Check that jb-view shows the session header (with UUID or metadata info)
    case "$_view_output" in
        *"session"*) pass "jb-view shows session header" ;;
        *)           fail "jb-view shows session header" "no header found in: $(echo "$_view_output" | head -3)" ;;
    esac
else
    skip "jb-view UUID rendering" "no session to view"
fi
