# test_session.sh — jb run creates .jb/sessions/<uuid>/ with the three files

repo_init

# Run a session
_out=$(prompt_pong | "$JB" run 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "jb run exits 0"
else
    fail "jb run exits 0" "got $_rc"
fi

# Session dir exists under .jb/sessions/
_latest=$(newest_session)
if [ -n "$_latest" ]; then
    pass "session dir created under .jb/sessions"
else
    fail "session dir created under .jb/sessions" "no dirs in $JB_SESSIONS_DIR"
fi

case "$_latest" in
    "$SCRATCH/.jb/sessions/"*) pass "session lives in the repo" ;;
    *)                         fail "session lives in the repo" "got: $_latest" ;;
esac

# UUID: 36 chars (32 hex + 4 dashes)
_uuid=$(basename "$_latest")
if [ "${#_uuid}" -eq 36 ]; then
    pass "session UUID is 36 characters"
else
    fail "session UUID is 36 characters" "got ${#_uuid}: $_uuid"
fi

# The three files
assert_file_exists "metadata.json created" "$_latest/metadata.json"
assert_file_exists "session.jsonl created" "$_latest/session.jsonl"
assert_file_exists "events.jsonl created" "$_latest/events.jsonl"

# No legacy wire-format files (hard cut-over: state.jsonl/log.jsonl deleted)
if [ -f "$_latest/state.jsonl" ] || [ -f "$_latest/log.jsonl" ]; then
    fail "no legacy state.jsonl/log.jsonl" "wire-format storage deleted"
else
    pass "no legacy state.jsonl/log.jsonl"
fi

# stderr banner names the session
_err=$(prompt_pong | "$JB" run 2>&1 >/dev/null)
case "$_err" in
    *"jb: session"*"started"*) pass "stderr banner: jb: session <id> started" ;;
    *)                         fail "stderr banner: jb: session <id> started" "got: $_err" ;;
esac

# No prompt on stdin → usage error, exit 2
_rc2=$(printf '' | "$JB" run >/dev/null 2>&1; echo $?)
if [ "$_rc2" -eq 2 ]; then
    pass "no prompt on stdin exits 2 (usage)"
else
    fail "no prompt on stdin exits 2 (usage)" "got $_rc2"
fi
