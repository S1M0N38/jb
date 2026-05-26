# test_parent.sh — --parent flag, jb tool, and session tree metadata

_cache_dir="${XDG_CACHE_HOME:-$HOME/.cache}/jb/sessions"

# --- Slice 1: --parent flag stores parent UUID in metadata ---

_parent_uuid="aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"
_output=$(echo "say OK" | "$JB" --parent "$_parent_uuid" 2>/dev/null)
_rc=$?

if [ "$_rc" -ne 0 ]; then
    fail "jb --parent runs successfully" "exit code $_rc"
else
    pass "jb --parent runs successfully"
fi

_latest=$(ls -td "$_cache_dir"/*/ 2>/dev/null | head -1)

if [ -z "$_latest" ]; then
    fail "parent metadata check" "no session dir found"
else
    _meta=$(cat "$_latest/metadata.json" 2>/dev/null)
    if command -v jq >/dev/null 2>&1; then
        _stored_parent=$(printf '%s' "$_meta" | jq -r '.parent // empty' 2>/dev/null)
        if [ "$_stored_parent" = "$_parent_uuid" ]; then
            pass "metadata has correct parent UUID"
        else
            fail "metadata has correct parent UUID" "expected $_parent_uuid, got: $_stored_parent"
        fi
    fi
fi

# --- Session without --parent should NOT have parent field ---

_output2=$(echo "say OK" | "$JB" 2>/dev/null)
_latest2=$(ls -td "$_cache_dir"/*/ 2>/dev/null | head -1)

if [ -n "$_latest2" ] && [ -f "$_latest2/metadata.json" ]; then
    _meta2=$(cat "$_latest2/metadata.json" 2>/dev/null)
    if command -v jq >/dev/null 2>&1; then
        _has_parent=$(printf '%s' "$_meta2" | jq 'has("parent")' 2>/dev/null)
        if [ "$_has_parent" = "false" ]; then
            pass "session without --parent has no parent field"
        else
            fail "session without --parent has no parent field" "parent field present: $_has_parent"
        fi
    fi
fi

# --- --parent with invalid UUID still stores it (no validation) ---

_funky_parent="not-a-real-uuid"
_output3=$(echo "say OK" | "$JB" --parent "$_funky_parent" 2>/dev/null)
_latest3=$(ls -td "$_cache_dir"/*/ 2>/dev/null | head -1)

if [ -n "$_latest3" ] && [ -f "$_latest3/metadata.json" ]; then
    _meta3=$(cat "$_latest3/metadata.json" 2>/dev/null)
    if command -v jq >/dev/null 2>&1; then
        _stored3=$(printf '%s' "$_meta3" | jq -r '.parent // empty' 2>/dev/null)
        if [ "$_stored3" = "$_funky_parent" ]; then
            pass "parent stored without validation"
        else
            fail "parent stored without validation" "expected $_funky_parent, got: $_stored3"
        fi
    fi
fi

# --- Slice 2+3: jb tool spawns child with parent linkage ---
# Clean sessions so we can identify parent/child cleanly
rm -rf "${_cache_dir:?}"/*/ 

# Run parent session that uses the jb tool
_out=$(echo "Use the jb tool with prompt 'say exactly: OK'. Reply with just what the child returns." | "$JB" 2>/dev/null)
_rc=$?

if [ "$_rc" -ne 0 ]; then
    fail "jb tool spawns child session" "exit code $_rc"
else
    case "$_out" in
        *OK*) pass "jb tool spawns child session and returns output" ;;
        *)    fail "jb tool spawns child session and returns output" "got: $(echo "$_out" | head -c 300)" ;;
    esac
fi

# Now we should have exactly 2 sessions: parent and child
# Find sessions that have children pointing to them
_dirs=$(ls -td "$_cache_dir"/*/ 2>/dev/null)
_dir_count=$(printf '%s\n' "$_dirs" | grep -c /)

if [ "$_dir_count" -ge 2 ]; then
    # Find the child: it's the session whose metadata has a parent field
    _child_dir=""
    _parent_dir=""
    _i=1
    for _d in $_dirs; do
        _pmeta=$(cat "${_d}metadata.json" 2>/dev/null)
        _pp=$(printf '%s' "$_pmeta" | jq -r '.parent // empty' 2>/dev/null)
        if [ -n "$_pp" ]; then
            _child_dir="$_d"
            _parent_uuid_val="$_pp"
            break
        fi
        _i=$((_i + 1))
    done

    if [ -n "$_child_dir" ] && [ -n "$_parent_uuid_val" ]; then
        # Verify the parent dir exists
        _parent_dir="${_cache_dir}/${_parent_uuid_val}"
        if [ -d "$_parent_dir" ]; then
            # Verify the parent has no parent field (it's a root)
            _parent_meta=$(cat "$_parent_dir/metadata.json" 2>/dev/null)
            _parent_has_parent=$(printf '%s' "$_parent_meta" | jq 'has("parent")' 2>/dev/null)
            if [ "$_parent_has_parent" = "false" ]; then
                pass "jb tool child has correct parent UUID"
            else
                fail "jb tool child has correct parent UUID" "parent session unexpectedly has a parent field"
            fi
        else
            fail "jb tool child has correct parent UUID" "parent dir not found: $_parent_dir"
        fi
    else
        fail "jb tool child has correct parent UUID" "no child session with parent field found"
    fi
else
    fail "jb tool parent-child check" "expected >= 2 sessions, got $_dir_count"
fi
