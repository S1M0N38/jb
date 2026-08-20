# test_config_flag.sh — --config flag behavior

repo_init

# --config with no value (bare flag at end) → exit 2 (usage), stderr message
_err=$(echo "hi" | "$JB" run --config 2>&1 >/dev/null)
_rc=$?
if [ "$_rc" -eq 2 ]; then
    pass "--config with no value exits 2 (usage)"
else
    fail "--config with no value exits 2 (usage)" "got $_rc"
fi
case "$_err" in
    *"config"*) pass "--config with no value prints error" ;;
    *)          fail "--config with no value prints error" "got: $_err" ;;
esac

# --config with nonexistent file → exit 1 (error)
_err2=$(echo "hi" | "$JB" run --config "$SCRATCH/nonexistent-config-test.json" 2>&1 >/dev/null)
_rc2=$?
if [ "$_rc2" -eq 1 ]; then
    pass "--config with missing file exits 1 (error)"
else
    fail "--config with missing file exits 1 (error)" "got $_rc2"
fi
case "$_err2" in
    *"config"*|*"No such"*|*"not found"*) pass "--config with missing file prints error" ;;
    *)          fail "--config with missing file prints error" "got: $_err2" ;;
esac

# --config specified twice → exit 2 (usage)
_tmpcfg="$SCRATCH/cfg-dup.json"
printf '{"api_url":"https://api.example.com/v1","model":"test-dup"}' > "$_tmpcfg"
_err3=$(echo "hi" | "$JB" run --config "$_tmpcfg" --config "$_tmpcfg" 2>&1 >/dev/null)
_rc3=$?
if [ "$_rc3" -eq 2 ]; then
    pass "--config twice exits 2 (usage)"
else
    fail "--config twice exits 2 (usage)" "got $_rc3"
fi
case "$_err3" in
    *"config"*|*"once"*|*"multiple"*) pass "--config twice prints error" ;;
    *)          fail "--config twice prints error" "got: $_err3" ;;
esac

# --config with valid file loads config (verify via metadata snapshot)
_tmpcfg2="$SCRATCH/cfg-valid.json"
_unique_model="test-config-flag-model"
cat > "$_tmpcfg2" <<EOF
{
  "api_url": "https://api.example.com/v1",
  "model": "$_unique_model",
  "max_tokens": 12345,
  "max_output_lines": 100,
  "max_output_bytes": 9999
}
EOF
_out=$(echo "say OK" | "$JB" run --config "$_tmpcfg2" 2>/dev/null)
_rc4=$?
_latest=$(newest_session)
if [ -n "$_latest" ] && [ -f "$_latest/metadata.json" ]; then
    _meta_model=$(jq -r '.config.model // empty' "$_latest/metadata.json" 2>/dev/null)
    if [ "$_meta_model" = "$_unique_model" ]; then
        pass "--config loads specified config file (model matches)"
    else
        fail "--config loads specified config file (model matches)" "expected $_unique_model, got $_meta_model"
    fi
    _meta_tokens=$(jq -r '.config.max_tokens // empty' "$_latest/metadata.json" 2>/dev/null)
    if [ "$_meta_tokens" = "12345" ]; then
        pass "--config loads specified config file (max_tokens matches)"
    else
        fail "--config loads specified config file (max_tokens matches)" "expected 12345, got $_meta_tokens"
    fi
else
    fail "--config loads specified config file" "no session metadata found"
fi

# --config with relative path resolves correctly
_rel_dir="$SCRATCH/rel"
mkdir -p "$_rel_dir"
cat > "$_rel_dir/rel-config.json" <<EOF
{
  "api_url": "https://api.example.com/v1",
  "model": "relative-path-test",
  "max_tokens": 99999
}
EOF
(
    cd "$_rel_dir" || exit 1
    echo "say OK" | "$JB" run --config rel-config.json >/dev/null 2>&1
)
_latest2=$(newest_session)
if [ -n "$_latest2" ] && [ -f "$_latest2/metadata.json" ]; then
    _meta_model2=$(jq -r '.config.model // empty' "$_latest2/metadata.json" 2>/dev/null)
    if [ "$_meta_model2" = "relative-path-test" ]; then
        pass "--config resolves relative path correctly"
    else
        fail "--config resolves relative path correctly" "expected relative-path-test, got $_meta_model2"
    fi
else
    fail "--config resolves relative path correctly" "no session metadata found"
fi

# Child jb process inherits --config from parent
# Derive the child config from the scratch copy of the real config
# (provider-agnostic), overriding max_tokens
_child_cfg="$SCRATCH/cfg-child.json"
_unique_max_tokens="77777"
jq --arg t "$_unique_max_tokens" '.max_tokens = ($t|tonumber)' "$SCRATCH/.config/jb/config.json" > "$_child_cfg" 2>/dev/null
# Fallback if no real config was copied: a minimal provider-neutral config
# (will fail API, but child inheritance is still verifiable via metadata)
if [ ! -s "$_child_cfg" ]; then
    cat > "$_child_cfg" <<'TESTCFG'
{
  "api_url": "https://api.example.com/v1",
  "model": "test-model",
  "max_tokens": 77777,
  "max_output_lines": 2000,
  "max_output_bytes": 51200
}
TESTCFG
fi

# Run parent that spawns a child via jb tool; both sessions land in this repo
_out=$(echo "Use the jb tool with prompt 'say exactly: OK'. Reply with just what the child returns." | "$JB" run --config "$_child_cfg" 2>/dev/null)
_rc_child=$?
if [ "$_rc_child" -eq 0 ]; then
    pass "child jb inherits --config: parent exits 0"
else
    fail "child jb inherits --config: parent exits 0" "exit $_rc_child"
fi

# Find the child session (has author field in metadata — the parent's uuid)
_found_child=0
for _d in "$JB_SESSIONS_DIR"/*/; do
    [ -f "${_d}metadata.json" ] || continue
    _has_author=$(jq -r '.author // empty' "${_d}metadata.json" 2>/dev/null)
    if [ -n "$_has_author" ]; then
        _c_tokens=$(jq -r '.config.max_tokens // empty' "${_d}metadata.json" 2>/dev/null)
        if [ "$_c_tokens" = "$_unique_max_tokens" ]; then
            pass "child jb inherits --config: child uses parent config"
            _found_child=1
        else
            fail "child jb inherits --config: child uses parent config" "expected max_tokens=$_unique_max_tokens, got $_c_tokens"
        fi
        break
    fi
done
if [ "$_found_child" -eq 0 ]; then
    fail "child jb inherits --config: child uses parent config" "no child session found"
fi
