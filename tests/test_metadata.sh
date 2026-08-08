# test_metadata.sh — Session metadata.json feature

# Run jb and verify metadata.json is created in session dir
_output=$(echo "say OK" | "$JB" 2>/dev/null)

_cache_dir="$JB_SESSIONS_DIR"
_latest=$(newest_session)

if [ -z "$_latest" ]; then
    fail "metadata.json created" "no session dir found"
else
    # metadata.json must exist
    assert_file_exists "metadata.json created" "$_latest/metadata.json"

    # Must be valid JSON
    if command -v jq >/dev/null 2>&1; then
        _meta=$(cat "$_latest/metadata.json" 2>/dev/null)
        _valid=$(printf '%s' "$_meta" | jq empty 2>&1)
        if [ $? -eq 0 ]; then
            pass "metadata.json is valid JSON"
        else
            fail "metadata.json is valid JSON" "$_valid"
        fi

        # Required fields present
        _has_uuid=$(printf '%s' "$_meta" | jq -r '.uuid // empty' 2>/dev/null)
        if [ -n "$_has_uuid" ]; then
            pass "metadata has uuid field"
        else
            fail "metadata has uuid field" "missing"
        fi

        _has_status=$(printf '%s' "$_meta" | jq -r '.status // empty' 2>/dev/null)
        if [ -n "$_has_status" ]; then
            pass "metadata has status field"
        else
            fail "metadata has status field" "missing"
        fi

        _has_title=$(printf '%s' "$_meta" | jq -r '.title // empty' 2>/dev/null)
        if [ -n "$_has_title" ]; then
            pass "metadata has title field"
        else
            fail "metadata has title field" "missing"
        fi

        _has_started=$(printf '%s' "$_meta" | jq -r '.started_at // empty' 2>/dev/null)
        if [ -n "$_has_started" ]; then
            pass "metadata has started_at field"
        else
            fail "metadata has started_at field" "missing"
        fi

        _has_model=$(printf '%s' "$_meta" | jq -r '.config.model // empty' 2>/dev/null)
        if [ -n "$_has_model" ]; then
            pass "metadata has config.model field"
        else
            fail "metadata has config.model field" "missing"
        fi

        _has_working_dir=$(printf '%s' "$_meta" | jq -r '.working_dir // empty' 2>/dev/null)
        if [ -n "$_has_working_dir" ]; then
            pass "metadata has working_dir field"
        else
            fail "metadata has working_dir field" "missing"
        fi

        # Close-phase fields (final metadata overwrite)
        _has_ended=$(printf '%s' "$_meta" | jq -r '.ended_at // empty' 2>/dev/null)
        if [ -n "$_has_ended" ]; then
            pass "metadata has ended_at field"
        else
            fail "metadata has ended_at field" "missing"
        fi

        _tokens=$(printf '%s' "$_meta" | jq -r '.tokens_used // empty' 2>/dev/null)
        if [ -n "$_tokens" ]; then
            pass "metadata has tokens_used field"
        else
            fail "metadata has tokens_used field" "missing"
        fi

        _turns=$(printf '%s' "$_meta" | jq -r '.turns // empty' 2>/dev/null)
        if [ -n "$_turns" ]; then
            pass "metadata has turns field"
        else
            fail "metadata has turns field" "missing"
        fi

        _exit_code=$(printf '%s' "$_meta" | jq -r '.exit_code // empty' 2>/dev/null)
        if [ -n "$_exit_code" ]; then
            pass "metadata has exit_code field"
        else
            fail "metadata has exit_code field" "missing"
        fi

        # Status should be 'completed' for successful run
        _status_val=$(printf '%s' "$_meta" | jq -r '.status // empty' 2>/dev/null)
        if [ "$_status_val" = "completed" ]; then
            pass "metadata status is 'completed' on success"
        else
            fail "metadata status is 'completed' on success" "got: $_status_val"
        fi
    fi
fi

# --- Title truncation test ---
_long_prompt="This is a very long prompt that exceeds the sixty character limit for titles and should be truncated"
_output2=$(printf '%s' "$_long_prompt" | "$JB" 2>/dev/null)

_latest2=$(newest_session)
if [ -n "$_latest2" ] && [ -f "$_latest2/metadata.json" ]; then
    _title2=$(jq -r '.title // empty' "$_latest2/metadata.json" 2>/dev/null)
    _title_len=${#_title2}
    # Title should be truncated (~64 chars max with ellipsis)
    if [ "$_title_len" -le 64 ]; then
        pass "title is truncated to ~64 chars (got $_title_len)"
    else
        fail "title is truncated to ~64 chars" "too long: $_title_len"
    fi
    # Truncated title should end with ... 
    case "$_title2" in
        *"...") pass "truncated title ends with ellipsis" ;;
        *)      fail "truncated title ends with ellipsis" "got: $_title2" ;;
    esac
fi
