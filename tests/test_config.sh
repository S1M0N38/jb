# test_config.sh — Goal 2: config loading

# jb exits 0 when config and API key are present
_actual=$(echo "say ok" | "$JB" >/dev/null 2>/dev/null; echo $?)
if [ "$_actual" -eq 0 ]; then
    pass "jb exits 0 with valid config"
else
    fail "jb exits 0 with valid config" "got $_actual"
fi
