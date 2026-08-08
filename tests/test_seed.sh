# test_seed.sh — --seed/--parent flags, $JB_SESSION env lineage, jb tool, session tree metadata

_cache_dir="$JB_SESSIONS_DIR"

# --- Slice 1: --seed flag stores weak link (spawned_from) in metadata ---

_seed_uuid="aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"
_output=$(echo "say OK" | "$JB" --seed "$_seed_uuid" 2>/dev/null)
_rc=$?

if [ "$_rc" -ne 0 ]; then
    fail "jb --seed runs successfully" "exit code $_rc"
else
    pass "jb --seed runs successfully"
fi

_latest=$(newest_session)

if [ -z "$_latest" ]; then
    fail "seed metadata check" "no session dir found"
else
    _meta=$(cat "$_latest/metadata.json" 2>/dev/null)
    if command -v jq >/dev/null 2>&1; then
        _stored_seed=$(printf '%s' "$_meta" | jq -r '.spawned_from // empty' 2>/dev/null)
        if [ "$_stored_seed" = "$_seed_uuid" ]; then
            pass "metadata has correct spawned_from UUID"
        else
            fail "metadata has correct spawned_from UUID" "expected $_seed_uuid, got: $_stored_seed"
        fi
        _has_parent=$(printf '%s' "$_meta" | jq 'has("parent")' 2>/dev/null)
        if [ "$_has_parent" = "false" ]; then
            pass "--seed does not set strong parent (weak link only)"
        else
            fail "--seed does not set strong parent" "parent field present: $_has_parent"
        fi
    fi
fi

# --- Slice 2: legacy --parent still works as alias for --seed ---

_legacy_uuid="bbbbbbbb-cccc-dddd-eeee-ffffffffffff"
_output1b=$(echo "say OK" | "$JB" --parent "$_legacy_uuid" 2>/dev/null)
_latest1b=$(newest_session)
if [ -n "$_latest1b" ] && [ -f "$_latest1b/metadata.json" ]; then
    _stored1b=$(jq -r '.spawned_from // empty' "$_latest1b/metadata.json" 2>/dev/null)
    if [ "$_stored1b" = "$_legacy_uuid" ]; then
        pass "--parent alias stores spawned_from"
    else
        fail "--parent alias stores spawned_from" "expected $_legacy_uuid, got: $_stored1b"
    fi
fi

# --- Slice 3: session without --seed should NOT have spawned_from field ---

_output2=$(echo "say OK" | "$JB" 2>/dev/null)
_latest2=$(newest_session)

if [ -n "$_latest2" ] && [ -f "$_latest2/metadata.json" ]; then
    _meta2=$(cat "$_latest2/metadata.json" 2>/dev/null)
    if command -v jq >/dev/null 2>&1; then
        _has_seed=$(printf '%s' "$_meta2" | jq 'has("spawned_from")' 2>/dev/null)
        if [ "$_has_seed" = "false" ]; then
            pass "session without --seed has no spawned_from field"
        else
            fail "session without --seed has no spawned_from field" "spawned_from present: $_has_seed"
        fi
    fi
fi

# --- Slice 4: --seed with invalid UUID still stores it (no validation) ---

_funky_seed="not-a-real-uuid"
_output3=$(echo "say OK" | "$JB" --seed "$_funky_seed" 2>/dev/null)
_latest3=$(newest_session)

if [ -n "$_latest3" ] && [ -f "$_latest3/metadata.json" ]; then
    _meta3=$(cat "$_latest3/metadata.json" 2>/dev/null)
    if command -v jq >/dev/null 2>&1; then
        _stored3=$(printf '%s' "$_meta3" | jq -r '.spawned_from // empty' 2>/dev/null)
        if [ "$_stored3" = "$_funky_seed" ]; then
            pass "seed stored without validation"
        else
            fail "seed stored without validation" "expected $_funky_seed, got: $_stored3"
        fi
    fi
fi

# --- Slice 5: $JB_SESSION env var sets weak link automatically ---

_env_uuid="cccccccc-dddd-eeee-ffff-000000000000"
_output4=$(JB_SESSION="$_env_uuid" echo "say OK" | JB_SESSION="$_env_uuid" "$JB" 2>/dev/null)
_latest4=$(newest_session)

if [ -n "$_latest4" ] && [ -f "$_latest4/metadata.json" ]; then
    _stored4=$(jq -r '.spawned_from // empty' "$_latest4/metadata.json" 2>/dev/null)
    if [ "$_stored4" = "$_env_uuid" ]; then
        pass "JB_SESSION env var sets spawned_from"
    else
        fail "JB_SESSION env var sets spawned_from" "expected $_env_uuid, got: $_stored4"
    fi
fi

# --- Slice 6: explicit --seed overrides $JB_SESSION env ---

_output5=$(JB_SESSION="$_env_uuid" echo "say OK" | JB_SESSION="$_env_uuid" "$JB" --seed "$_seed_uuid" 2>/dev/null)
_latest5=$(newest_session)

if [ -n "$_latest5" ] && [ -f "$_latest5/metadata.json" ]; then
    _stored5=$(jq -r '.spawned_from // empty' "$_latest5/metadata.json" 2>/dev/null)
    if [ "$_stored5" = "$_seed_uuid" ]; then
        pass "--seed flag overrides JB_SESSION env"
    else
        fail "--seed flag overrides JB_SESSION env" "expected $_seed_uuid, got: $_stored5"
    fi
fi

# --- Slice 7: jb tool spawns child with weak link (spawned_from) ---
# No shared-cache cleanup needed: this scratch starts empty and holds only
# this file's sessions; child detection is by the spawned_from field

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
    # Find the child: it's the session whose metadata has spawned_from
    _child_dir=""
    _parent_dir=""
    for _d in $_dirs; do
        _pp=$(jq -r '.spawned_from // empty' "${_d}metadata.json" 2>/dev/null)
        if [ -n "$_pp" ]; then
            _child_dir="$_d"
            _parent_uuid_val="$_pp"
            break
        fi
    done

    if [ -n "$_child_dir" ] && [ -n "$_parent_uuid_val" ]; then
        # Verify the parent dir exists
        _parent_dir="${_cache_dir}/${_parent_uuid_val}"
        if [ -d "$_parent_dir" ]; then
            # Verify the parent has no spawned_from (it's a root)
            _parent_has_spawned=$(jq 'has("spawned_from")' "$_parent_dir/metadata.json" 2>/dev/null)
            if [ "$_parent_has_spawned" = "false" ]; then
                pass "jb tool child has correct spawned_from UUID"
            else
                fail "jb tool child has correct spawned_from UUID" "parent session unexpectedly has spawned_from"
            fi
        else
            fail "jb tool child has correct spawned_from UUID" "parent dir not found: $_parent_dir"
        fi
    else
        fail "jb tool child has correct spawned_from UUID" "no child session with spawned_from found"
    fi
else
    fail "jb tool parent-child check" "expected >= 2 sessions, got $_dir_count"
fi
