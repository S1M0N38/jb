# test_status.sh — jb status: the repo glance (§7)
# Action-oriented: session line (or working: N), children, awaiting commit,
# and the always-on repo summary. Stale $JB_SESSION → warning, exit 0.

repo_init

# five sessions with distinct start times (start_ago seconds; smaller = newer):
#   parent(working, 500s)  child_new(working, 120s)  child_old(error, 400s)
#   child_cmt(committed, 300s)  other(completed, 200s, author "")
_parent="21637177a1b2c3d4e5f60718293a4b5c6"
_child_new="7b2e1d44a1b2c3d4e5f60718293a4b5c7"
_child_old="b3586600a1b2c3d4e5f60718293a4b5c8"
_child_cmt="9f3c2a1ba1b2c3d4e5f60718293a4b5c9"
_other="c47d2e01a1b2c3d4e5f60718293a4b5ca"
fixture_session "$_parent" working "the parent" "" "" 500
fixture_session "$_child_new" working "child new" "$_parent" "" 120
fixture_session "$_child_old" error "child old" "$_parent" "" 400
fixture_session "$_child_cmt" committed "child committed" "$_parent" "" 300
fixture_session "$_other" completed "unrelated" "" "" 200

# ---- with $JB_SESSION: session line, children, awaiting, summary ----
_out=$(JB_SESSION="$_parent" "$JB" status 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "jb status exits 0"
else
    fail "jb status exits 0" "got $_rc"
fi

# session line: identity, status, subject, age while working (first line)
_l1=$(printf '%s\n' "$_out" | sed -n '1p')
case "$_l1" in
    "session 21637177  (working)  \"the parent\" · "*m) pass "jb status session line (working, age)" ;;
    *) fail "jb status session line (working, age)" "got: $_l1" ;;
esac

# children section: pending count, ps-style lines, committed count
case "$_out" in
    *"children: 2"*) pass "jb status lists pending children count" ;;
    *) fail "jb status lists pending children count" "got: $_out" ;;
esac
case "$_out" in
    *"  7b2e1d44  working"*"  b3586600  error"*) pass "jb status children are ps-format, newest first" ;;
    *) fail "jb status children are ps-format, newest first" "got: $_out" ;;
esac
case "$_out" in
    *"committed: 1"*) pass "jb status counts committed children" ;;
    *) fail "jb status counts committed children" "got: $_out" ;;
esac

# awaiting commit: completed/error, not committed, newest first, ids only
case "$_out" in
    *"awaiting commit: 2 (c47d2e01, b3586600)"*) pass "jb status awaiting commit (newest first)" ;;
    *) fail "jb status awaiting commit (newest first)" "got: $_out" ;;
esac

# the always-on repo summary — exact counts from the fixtures
case "$_out" in
    *"repo: 5 sessions · 1 committed · 2 working · 1 completed · 1 error"*) pass "jb status repo summary counts" ;;
    *) fail "jb status repo summary counts" "got: $_out" ;;
esac

# ---- terminal @: "ended … ago" (first line) ----
_out=$(JB_SESSION="$_other" "$JB" status 2>/dev/null)
_l1=$(printf '%s\n' "$_out" | sed -n '1p')
case "$_l1" in
    "session c47d2e01  (completed)  \"unrelated\" · ended "*[smhd]" ago") pass "jb status terminal session shows ended ago" ;;
    *) fail "jb status terminal session shows ended ago" "got: $_l1" ;;
esac

# ---- no $JB_SESSION: working list + summary, exit 0 ----
_out=$(env -u JB_SESSION "$JB" status 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "jb status without JB_SESSION exits 0"
else
    fail "jb status without JB_SESSION exits 0" "got $_rc"
fi
case "$_out" in
    "working: 2 (7b2e1d44 \"child new\", 21637177 \"the parent\")"*) pass "jb status working list without JB_SESSION" ;;
    *) fail "jb status working list without JB_SESSION" "got: $(echo "$_out" | head -1)" ;;
esac
case "$_out" in
    *"repo: 5 sessions"*) pass "jb status summary without JB_SESSION" ;;
    *) fail "jb status summary without JB_SESSION" "got: $_out" ;;
esac

# no session line, no children section in the no-session view
case "$_out" in
    *"session 21637177"*) fail "jb status omits session line without JB_SESSION" ;;
    *) pass "jb status omits session line without JB_SESSION" ;;
esac

# ---- stale $JB_SESSION: warning, falls back, exit 0 ----
_err=$(JB_SESSION="deadbeef00000000000000000000000000" "$JB" status 2>&1 >/dev/null)
_out=$(JB_SESSION="deadbeef00000000000000000000000000" "$JB" status 2>/dev/null)
_rc=$?
case "$_err" in
    *"JB_SESSION deadbeef"*) pass "jb status warns on stale JB_SESSION" ;;
    *) fail "jb status warns on stale JB_SESSION" "got: $_err" ;;
esac
case "$_out" in
    *"repo: 5 sessions"*) pass "jb status falls back to no-session view" ;;
    *) fail "jb status falls back to no-session view" "got: $_out" ;;
esac
if [ "$_rc" -eq 0 ]; then
    pass "jb status stale JB_SESSION exits 0"
else
    fail "jb status stale JB_SESSION exits 0" "got $_rc"
fi

# ---- outside a repo ----
new_scratch
"$JB" status >/dev/null 2>&1
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "jb status outside a repo exits 1"
else
    fail "jb status outside a repo exits 1" "got $_rc"
fi
