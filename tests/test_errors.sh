# test_errors.sh — API error handling

repo_init

# jb run exits 1 on invalid API key (non-retryable error)
_actual=$(echo "hi" | env -i HOME="$SCRATCH" XDG_CONFIG_HOME="$SCRATCH/.config" JB_API_KEY="sk-invalid-test-key" PATH="$PATH" "$JB" run 2>/dev/null; echo $?)
if [ "$_actual" -eq 1 ]; then
    pass "jb run exits 1 on invalid API key"
else
    fail "jb run exits 1 on invalid API key" "got $_actual"
fi

# A failed run still records the session as error in metadata
_latest=$(newest_session)
if [ -n "$_latest" ] && [ -f "$_latest/metadata.json" ]; then
    _s=$(jq -r '.status // empty' "$_latest/metadata.json" 2>/dev/null)
    if [ "$_s" = "error" ]; then
        pass "failed run leaves metadata status error"
    else
        fail "failed run leaves metadata status error" "got: $_s"
    fi
else
    fail "failed run leaves metadata status error" "no session dir found"
fi

# Missing config (no HOME, no XDG_CONFIG_HOME) → exit 1 (was 3)
_rc2=$(echo "hi" | env -i HOME="$SCRATCH/nohome" PATH="$PATH" "$JB" run 2>/dev/null; echo $?)
if [ "$_rc2" -eq 1 ]; then
    pass "missing config exits 1 (error)"
else
    fail "missing config exits 1 (error)" "got $_rc2"
fi
