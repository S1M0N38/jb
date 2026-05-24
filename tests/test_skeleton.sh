# test_skeleton.sh — Goal 1: build system + skeleton

# jb binary exists and is executable
[ -x "$JB" ] && pass "jb binary exists" || fail "jb binary exists"

# jb exits 3 when config/api-key missing
_actual=$(echo "hello" | env -i HOME="/tmp/_jb_missing_$$" PATH="$PATH" "$JB" 2>/dev/null; echo $?)
if [ "$_actual" -eq 3 ]; then
    pass "jb exits 3 on missing config"
else
    fail "jb exits 3 on missing config" "got $_actual"
fi
