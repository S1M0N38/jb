# test_errors.sh — API error handling

# jb exits 1 on invalid API key (non-retryable error)
_actual=$(echo "hi" | env -i HOME="$SCRATCH" XDG_CONFIG_HOME="$SCRATCH/.config" JB_API_KEY="sk-invalid-test-key" PATH="$PATH" "$JB" 2>/dev/null; echo $?)
if [ "$_actual" -eq 1 ]; then
    pass "jb exits 1 on invalid API key"
else
    fail "jb exits 1 on invalid API key" "got $_actual"
fi
