# test_flags.sh — --version, --help, bare jb, unknown flags

# --version prints version string and exits 0
assert_stdout_contains "jb --version prints version" "jb 0.2" "$JB" --version
assert_exit 0 "jb --version exits 0" "$JB" --version

# --help prints the command reference and exits 0
assert_stdout_contains "jb --help mentions jb" "usage: jb" "$JB" --help
assert_exit 0 "jb --help exits 0" "$JB" --help

# Bare jb with piped stdin is still help (hard cut-over: bare jb = jb help)
_help_out=$(echo "say OK" | "$JB" 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "bare jb with piped stdin exits 0 (help, not run)"
else
    fail "bare jb with piped stdin exits 0 (help, not run)" "got $_rc"
fi
case "$_help_out" in
    *"usage: jb"*) pass "bare jb prints help even with piped stdin" ;;
    *)             fail "bare jb prints help even with piped stdin" "got: $(echo "$_help_out" | head -c 120)" ;;
esac

# Unknown option → usage error, exit 2 (no run attempt)
_err=$(echo "hi" | "$JB" --bogus 2>&1 >/dev/null)
_rc2=$?
if [ "$_rc2" -eq 2 ]; then
    pass "unknown option exits 2 (usage)"
else
    fail "unknown option exits 2 (usage)" "got $_rc2"
fi
case "$_err" in
    *"unknown option"*) pass "unknown option prints error" ;;
    *)                  fail "unknown option prints error" "got: $_err" ;;
esac

# -C without a value → usage error, exit 2
_rc3=$("$JB" -C 2>/dev/null; echo $?)
if [ "$_rc3" -eq 2 ]; then
    pass "-C without a value exits 2 (usage)"
else
    fail "-C without a value exits 2 (usage)" "got $_rc3"
fi
