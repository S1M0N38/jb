# test_wait.sh — jb wait ID (§7)
# Polls metadata.json every 250 ms until status ∈ {completed, error}.
# stdout silent; stderr "jb: waiting for <id8>…" at start; refuses the
# current session (self-deadlock).

repo_init

_uuid="21637177a1b2c3d4e5f60718293a4b5c6"
fixture_session "$_uuid" completed "already done"
_meta="$JB_SESSIONS_DIR/$_uuid/metadata.json"

# already-terminal (completed) → returns immediately, exit 0
_out=$("$JB" wait "$_uuid" 2>&1)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "jb wait on a completed session exits 0"
else
    fail "jb wait on a completed session exits 0" "got $_rc"
fi
case "$_out" in
    *"jb: waiting for 21637177…"*) pass "jb wait prints the waiting line to stderr" ;;
    *) fail "jb wait prints the waiting line to stderr" "got: $_out" ;;
esac
# stdout is silent (captured separately — no $'...' quoting)
_so=$("$JB" wait "$_uuid" 2>/dev/null)
if [ -z "$_so" ]; then
    pass "jb wait keeps stdout silent"
else
    fail "jb wait keeps stdout silent" "got: $_so"
fi

# error status → exit 1
fixture_session "b3586600a1b2c3d4e5f60718293a4b5c7" error "failed run"
"$JB" wait "b3586600a1b2c3d4e5f60718293a4b5c7" >/dev/null 2>&1
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "jb wait on an error session exits 1"
else
    fail "jb wait on an error session exits 1" "got $_rc"
fi

# working session that transitions → polls until terminal, exit 0
fixture_session "7b2e1d44a1b2c3d4e5f60718293a4b5c8" working "will finish"
_meta2="$JB_SESSIONS_DIR/7b2e1d44a1b2c3d4e5f60718293a4b5c8/metadata.json"
( sleep 1
  jq '.status = "completed" | .ended_at = .last_activity' "$_meta2" > "$_meta2.tmp" \
      && mv "$_meta2.tmp" "$_meta2" ) &
"$JB" wait "7b2e1d44a1b2c3d4e5f60718293a4b5c8" >/dev/null 2>&1 &
_wpid=$!
_slept=0
while kill -0 "$_wpid" 2>/dev/null && [ "$_slept" -lt 500 ]; do
    sleep 0.1; _slept=$((_slept + 1))
done
if kill -0 "$_wpid" 2>/dev/null; then
    kill "$_wpid" 2>/dev/null
    fail "jb wait polls until the session is terminal" "still waiting after 50s"
else
    wait "$_wpid"; _rc=$?
    if [ "$_rc" -eq 0 ]; then
        pass "jb wait polls until the session is terminal"
    else
        fail "jb wait polls until the session is terminal" "exit $_rc"
    fi
fi

# refuses the current session (self-deadlock), exit 1
_err=$(JB_SESSION="$_uuid" "$JB" wait "$_uuid" 2>&1 >/dev/null)
_rc=$?
case "$_err" in
    *"cannot wait on the current session"*) pass "jb wait refuses the current session" ;;
    *) fail "jb wait refuses the current session" "got: $_err" ;;
esac
if [ "$_rc" -eq 1 ]; then
    pass "jb wait current-session refusal exits 1"
else
    fail "jb wait current-session refusal exits 1" "got $_rc"
fi

# @ resolves via JB_SESSION (the error session is not the target)
JB_SESSION="b3586600a1b2c3d4e5f60718293a4b5c7" "$JB" wait @ >/dev/null 2>&1
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "jb wait @ resolves via JB_SESSION"
else
    fail "jb wait @ resolves via JB_SESSION" "got $_rc"
fi

# unknown id → exit 1
env -u JB_SESSION "$JB" wait xyz >/dev/null 2>&1
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "jb wait <unknown> exits 1"
else
    fail "jb wait <unknown> exits 1" "got $_rc"
fi

# outside a repo → exit 1
new_scratch
"$JB" wait "$_uuid" >/dev/null 2>&1
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "jb wait outside a repo exits 1"
else
    fail "jb wait outside a repo exits 1" "got $_rc"
fi
