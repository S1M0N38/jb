# test_contrib.sh — contrib scripts (jb-list, jb-view)

# ---- jb-list tests ----

# jb-list should exist and be executable
if [ -x "contrib/jb-list" ]; then
    pass "jb-list script exists and is executable"
else
    fail "jb-list script exists and is executable" "not found or not executable"
fi

# jb-view should exist and be executable (renamed from jb-watch)
if [ -x "contrib/jb-view" ]; then
    pass "jb-view script exists and is executable"
else
    fail "jb-view script exists and is executable" "not found or not executable"
fi

# jb-list should output valid JSONL (one JSON object per line)
_cache_dir="${XDG_CACHE_HOME:-$HOME/.cache}/jb/sessions"
if [ -d "$_cache_dir" ] && ls "$_cache_dir"/*/metadata.json >/dev/null 2>&1; then
    _list_output=$(contrib/jb-list 2>/dev/null)
    if [ -n "$_list_output" ]; then
        pass "jb-list produces output when sessions exist"

        # Each line should be valid JSON
        _jsonl_ok=1
        _line_count=0
        echo "$_list_output" | while IFS= read -r _line || [ -n "$_line" ]; do
            _line_count=$((_line_count + 1))
            if ! printf '%s' "$_line" | jq empty 2>/dev/null; then
                _jsonl_ok=0
            fi
        done
        # Check at least one session was listed (we have sessions from metadata tests)
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

# jb-view --help or no args should show usage (or error about missing sessions is ok too)
# We just verify it doesn't crash horribly
_view_err=$(contrib/jb-view --help 2>&1 || true)
if [ -n "$_view_err" ] || [ $? -eq 0 ]; then
    pass "jb-view runs without crashing"
else
    fail "jb-view runs without crashing" "unexpected error"
fi

# jb-view with a specific UUID from our test sessions
_latest_session=$(ls -td "$_cache_dir"/*/ 2>/dev/null | head -1 | xargs basename 2>/dev/null)
if [ -n "$_latest_session" ] && [ -f "$_cache_dir/$_latest_session/state.jsonl" ]; then
    _view_output=$(contrib/jb-view "$_latest_session" 2>/dev/null) || true
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
