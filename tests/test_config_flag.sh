# test_config_flag.sh — --config flag behavior

# --config with no value (bare flag at end) → exit 3, stderr message
_err=$(echo "hi" | "$JB" --config 2>&1 >/dev/null)
_rc=$?
if [ "$_rc" -eq 3 ]; then
    pass "--config with no value exits 3"
else
    fail "--config with no value exits 3" "got $_rc"
fi
case "$_err" in
    *"config"*) pass "--config with no value prints error" ;;
    *)          fail "--config with no value prints error" "got: $_err" ;;
esac

# --config with nonexistent file → exit 3, stderr message
_err2=$(echo "hi" | "$JB" --config /tmp/jb-nonexistent-config-test.json 2>&1 >/dev/null)
_rc2=$?
if [ "$_rc2" -eq 3 ]; then
    pass "--config with missing file exits 3"
else
    fail "--config with missing file exits 3" "got $_rc2"
fi
case "$_err2" in
    *"config"*|*"No such"*|*"not found"*) pass "--config with missing file prints error" ;;
    *)          fail "--config with missing file prints error" "got: $_err2" ;;
esac

# --config specified twice → exit 3, stderr message
_tmpcfg=$(mktemp)
printf '{"model":"gpt-4.1"}' > "$_tmpcfg"
_err3=$(echo "hi" | "$JB" --config "$_tmpcfg" --config "$_tmpcfg" 2>&1 >/dev/null)
_rc3=$?
if [ "$_rc3" -eq 3 ]; then
    pass "--config twice exits 3"
else
    fail "--config twice exits 3" "got $_rc3"
fi
case "$_err3" in
    *"config"*|*"once"*|*"multiple"*) pass "--config twice prints error" ;;
    *)          fail "--config twice prints error" "got: $_err3" ;;
esac
rm -f "$_tmpcfg"

# --config with valid file loads config (verify via metadata)
_tmpcfg2=$(mktemp)
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
_out=$(echo "say OK" | "$JB" --config "$_tmpcfg2" 2>/dev/null)
_rc4=$?
_cache_dir="${XDG_CACHE_HOME:-$HOME/.cache}/jb/sessions"
_latest=$(ls -td "$_cache_dir"/*/ 2>/dev/null | head -1)
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
rm -f "$_tmpcfg2"

# --config with relative path resolves correctly
_mkdir=$(mktemp -d)
_rel_cfg="$_mkdir/rel-config.json"
cat > "$_rel_cfg" <<EOF
{
  "model": "relative-path-test",
  "max_tokens": 99999
}
EOF
cd "$_mkdir"
_out=$(echo "say OK" | "$OLDPWD/jb" --config rel-config.json 2>/dev/null)
_rc5=$?
cd "$OLDPWD"
_latest2=$(ls -td "$_cache_dir"/*/ 2>/dev/null | head -1)
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
rm -rf "$_mkdir"

# Child jb process inherits --config from parent
# Run parent with --config that uses the jb tool; verify child also used same config
_child_cfg=$(mktemp)
_unique_max_tokens="77777"
cat > "$_child_cfg" <<'TESTCFG'
{
  "api_url": "https://api.z.ai/api/coding/paas/v4",
  "model": "glm-5.1",
  "max_tokens": 77777,
  "max_output_lines": 2000,
  "max_output_bytes": 51200
}
TESTCFG
# Clean sessions for clean child detection
_cache_dir2="${XDG_CACHE_HOME:-$HOME/.cache}/jb/sessions"
rm -rf "${_cache_dir2:?}"/*/

# Run parent that spawns a child via jb tool
_out=$(echo "Use the jb tool with prompt 'say exactly: OK'. Reply with just what the child returns." | "$JB" --config "$_child_cfg" 2>/dev/null)
_rc_child=$?
if [ "$_rc_child" -eq 0 ]; then
    pass "child jb inherits --config: parent exits 0"
else
    fail "child jb inherits --config: parent exits 0" "exit $_rc_child"
fi

# Find the child session (has parent field in metadata)
_found_child=0
for _d in "$_cache_dir2"/*/; do
    [ -f "${_d}metadata.json" ] || continue
    _has_p=$(jq 'has("parent")' "${_d}metadata.json" 2>/dev/null)
    if [ "$_has_p" = "true" ]; then
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
rm -f "$_child_cfg"
