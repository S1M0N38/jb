# test_ps.sh — jb ps: my children (§7)
# Children = sessions with author == @. Pending lines
# id<TAB>status<TAB>age<TAB>subject (newest first), then committed: N.

repo_init

# parent @ and three children: one working, one error (older), one committed
_parent="21637177a1b2c3d4e5f60718293a4b5c6"
_child_new="7b2e1d44a1b2c3d4e5f60718293a4b5c7"
_child_old="b3586600a1b2c3d4e5f60718293a4b5c8"
_child_cmt="9f3c2a1ba1b2c3d4e5f60718293a4b5c9"
_other="c47d2e01a1b2c3d4e5f60718293a4b5ca"
fixture_session "$_parent" working "the parent"
fixture_session "$_child_new" working "child new" "$_parent"
fixture_session "$_child_old" error "child old" "$_parent" "" 400
fixture_session "$_child_cmt" committed "child committed" "$_parent"
fixture_session "$_other" completed "unrelated" "" 

# pending children, newest first, ps format
_out=$(JB_SESSION="$_parent" "$JB" ps 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "jb ps exits 0"
else
    fail "jb ps exits 0" "got $_rc"
fi
_l1=$(printf '%s\n' "$_out" | sed -n '1p')
_l2=$(printf '%s\n' "$_out" | sed -n '2p')
# line shape: 8-hex id<TAB>status<TAB>age<TAB>"subject"
case "$_l1" in
    "7b2e1d44	working	"*'"child new"') pass "jb ps lists the newest pending child first" ;;
    *) fail "jb ps lists the newest pending child first" "got: $_l1" ;;
esac
case "$_l2" in
    "b3586600	error	"*'"child old"') pass "jb ps lists the older pending child second" ;;
    *) fail "jb ps lists the older pending child second" "got: $_l2" ;;
esac
case "$_l1" in
    *	[0-9]*:[0-9][0-9]	*) pass "jb ps age is mm:ss" ;;
    *) fail "jb ps age is mm:ss" "got: $_l1" ;;
esac
case "$_out" in
    *"committed: 1"*) pass "jb ps counts committed children" ;;
    *) fail "jb ps counts committed children" "got: $_out" ;;
esac
# unrelated session (author "") is not a child
case "$_out" in
    *"c47d2e01"*) fail "jb ps excludes non-children" "got: $_out" ;;
    *) pass "jb ps excludes non-children" ;;
esac

# no $JB_SESSION → exit 1
env -u JB_SESSION "$JB" ps >/dev/null 2>&1
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "jb ps without JB_SESSION exits 1"
else
    fail "jb ps without JB_SESSION exits 1" "got $_rc"
fi

# no children → empty stdout, exit 0
_out=$(JB_SESSION="$_other" "$JB" ps 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ] && [ -z "$_out" ]; then
    pass "jb ps with no children prints nothing"
else
    fail "jb ps with no children prints nothing" "exit $_rc, out: $_out"
fi

# stale $JB_SESSION → exit 1, error message
_err=$(JB_SESSION="deadbeef00000000000000000000000000" "$JB" ps 2>&1 >/dev/null)
_rc=$?
case "$_err" in
    *"JB_SESSION deadbeef"*) pass "jb ps reports a stale JB_SESSION" ;;
    *) fail "jb ps reports a stale JB_SESSION" "got: $_err" ;;
esac
if [ "$_rc" -eq 1 ]; then
    pass "jb ps stale JB_SESSION exits 1"
else
    fail "jb ps stale JB_SESSION exits 1" "got $_rc"
fi

# outside a repo → exit 1
new_scratch
"$JB" ps >/dev/null 2>&1
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "jb ps outside a repo exits 1"
else
    fail "jb ps outside a repo exits 1" "got $_rc"
fi
