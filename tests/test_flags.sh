# test_flags.sh — --version, --help, tty detection

# --version prints version string and exits 0
assert_stdout_contains "jb --version prints version" "jb 0.1" "$JB" --version
assert_exit 0 "jb --version exits 0" "$JB" --version

# --help prints usage hint and exits 0
assert_stdout_contains "jb --help mentions man page" "jb(1)" "$JB" --help
assert_exit 0 "jb --help exits 0" "$JB" --help

# Unknown flag: jb ignores it, prompt comes from stdin, so it runs normally
# With valid API key it should succeed (exit 0)
_out=$(echo "respond with exactly: OK" | "$JB" --bogus 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "jb ignores unknown flags and runs normally"
else
    fail "jb ignores unknown flags and runs normally" "exit $_rc"
fi

# No flags, stdin has data — runs normally (exit 0 with valid API key)
_actual=$(echo "say OK" | "$JB" >/dev/null 2>/dev/null; echo $?)
if [ "$_actual" -eq 0 ]; then
    pass "jb runs with piped stdin"
else
    fail "jb runs with piped stdin" "got $_actual"
fi
