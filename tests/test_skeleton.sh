# test_skeleton.sh — dispatch: jb init, bare jb = help

# jb binary exists and is executable
[ -x "$JB" ] && pass "jb binary exists" || fail "jb binary exists"

# ---- Bare jb = jb help (hard cut-over) ----

# Bare jb prints the command reference on stdout and exits 0
_help_out=$("$JB" </dev/null 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "bare jb exits 0 (help)"
else
    fail "bare jb exits 0 (help)" "got $_rc"
fi
case "$_help_out" in
    *"usage: jb"*|*"jb help"*) pass "bare jb prints help" ;;
    *)                        fail "bare jb prints help" "got: $(echo "$_help_out" | head -c 120)" ;;
esac

# jb help exits 0 and names the verbs
_help_out2=$("$JB" help 2>/dev/null)
_rc2=$?
if [ "$_rc2" -eq 0 ]; then
    pass "jb help exits 0"
else
    fail "jb help exits 0" "got $_rc2"
fi
case "$_help_out2" in
    *"init"*) pass "jb help lists init" ;;
    *)        fail "jb help lists init" "got: $(echo "$_help_out2" | head -c 120)" ;;
esac
case "$_help_out2" in
    *"run"*) pass "jb help lists run" ;;
    *)       fail "jb help lists run" "got: $(echo "$_help_out2" | head -c 120)" ;;
esac

# Unknown command → usage error, exit 2
_unknown_err=$("$JB" frobnicate 2>&1 >/dev/null)
_rc3=$?
if [ "$_rc3" -eq 2 ]; then
    pass "unknown command exits 2 (usage)"
else
    fail "unknown command exits 2 (usage)" "got $_rc3"
fi
case "$_unknown_err" in
    *"unknown command"*"frobnicate"*) pass "unknown command names the verb" ;;
    *)                                fail "unknown command names the verb" "got: $_unknown_err" ;;
esac

# Unknown option → usage error, exit 2
_opt_err=$("$JB" --bogus 2>&1 >/dev/null)
_rc4=$?
if [ "$_rc4" -eq 2 ]; then
    pass "unknown option exits 2 (usage)"
else
    fail "unknown option exits 2 (usage)" "got $_rc4"
fi

# ---- jb init ----

# jb init creates .jb/ with sessions/ and config.json
_init_out=$("$JB" init 2>&1)
_rc5=$?
if [ "$_rc5" -eq 0 ]; then
    pass "jb init exits 0"
else
    fail "jb init exits 0" "got $_rc5"
fi
assert_dir_exists "jb init creates .jb/" "$SCRATCH/.jb"
assert_dir_exists "jb init creates .jb/sessions/" "$SCRATCH/.jb/sessions"
assert_file_exists "jb init creates .jb/config.json" "$SCRATCH/.jb/config.json"
case "$_init_out" in
    *"initialized empty jb repository"*) pass "jb init prints initialized message" ;;
    *)                                   fail "jb init prints initialized message" "got: $_init_out" ;;
esac
case "$_init_out" in
    *"$(pwd -P)"*) pass "jb init message names the repo dir" ;;
    *)              fail "jb init message names the repo dir" "got: $_init_out" ;;
esac

# config.json is an empty object
_cfg=$(cat "$SCRATCH/.jb/config.json" 2>/dev/null)
case "$_cfg" in
    *"{}"*) pass "jb init writes empty config {}" ;;
    *)      fail "jb init writes empty config {}" "got: $_cfg" ;;
esac

# jb init is idempotent → reinitialized
_init2=$("$JB" init 2>&1)
_rc6=$?
if [ "$_rc6" -eq 0 ]; then
    pass "jb init idempotent: exits 0"
else
    fail "jb init idempotent: exits 0" "got $_rc6"
fi
case "$_init2" in
    *"reinitialized existing jb repository"*) pass "jb init idempotent: reinitialized message" ;;
    *)                                        fail "jb init idempotent: reinitialized message" "got: $_init2" ;;
esac
