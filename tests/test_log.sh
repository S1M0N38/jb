# test_log.sh — jb log [--graph] (§7)
# Flat: id<TAB>status<TAB>author<TAB>age<TAB>subject, newest first.
# --graph: the committed forest — roots oldest-first, children by start
# time, <id> [error] <kind> "<subject>", kind root|fork|fresh.

repo_init

# committed forest: A(root) → B(fork), C(fork, error); D(root) → E(fork);
# F(fresh: spawned by A, no --fork parent). G(working) and X(completed)
# are absent from the graph. start_ago: A=600 B=550 C=500 D=400 E=350
# F=300 G=200 X=150 (smaller = newer).
_a="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
_b="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
_c="cccccccccccccccccccccccccccccccc"
_d="dddddddddddddddddddddddddddddddd"
_e="eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"
_f="ffffffffffffffffffffffffffffffff"
_g="gggggggggggggggggggggggggggggggg"
_x="deadbeef000000000000000000000000"
fixture_session "$_a" committed "root A subject" "" "" 600
fixture_session "$_b" committed "fork B subject" "$_a" "$_a" 550
fixture_session "$_c" committed "fork C subject" "$_a" "$_a" 500 1
fixture_session "$_d" committed "root D subject" "" "" 400
fixture_session "$_e" committed "fork E subject" "$_d" "$_d" 350
fixture_session "$_f" committed "fresh F subject" "$_a" "" 300
fixture_session "$_g" working "working G" "$_a" "$_a" 200
fixture_session "$_x" completed "completed X" "" "" 150

# ---- flat log ----
_out=$("$JB" log 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "jb log exits 0"
else
    fail "jb log exits 0" "got $_rc"
fi

# newest first: X(150) … A(600)
_l1=$(printf '%s\n' "$_out" | sed -n '1p')
case "$_l1" in
    "deadbeef	completed	-	"*) pass "jb log lists newest first (X first)" ;;
    *) fail "jb log lists newest first (X first)" "got: $_l1" ;;
esac
_ll=$(printf '%s\n' "$_out" | tail -1)
case "$_ll" in
    "aaaaaaaa	committed	-	"*) pass "jb log lists oldest last (A last)" ;;
    *) fail "jb log lists oldest last (A last)" "got: $_ll" ;;
esac

# format: id<TAB>status<TAB>author<TAB>age<TAB>subject
case "$_l1" in
    deadbeef	completed	-	[0-9]*[smhd]	"completed X") pass "jb log line format id/status/author/age/subject" ;;
    *) fail "jb log line format id/status/author/age/subject" "got: $_l1" ;;
esac

# author: short id for children, "-" for human runs
case "$_out" in
    *"bbbbbbbb	committed	aaaaaaaa	"*) pass "jb log author is the parent's short id" ;;
    *) fail "jb log author is the parent's short id" "got: $_out" ;;
esac

# every session is listed (8 lines, incl. working + completed)
_n=$(printf '%s\n' "$_out" | wc -l | tr -d ' ')
if [ "$_n" -eq 8 ]; then
    pass "jb log lists all sessions"
else
    fail "jb log lists all sessions" "got $_n lines"
fi

# subject ≤ 40 chars (truncate a long subject)
fixture_session "1234567890abcdef1234567890abcdef" completed \
    "this subject is far too long and must be truncated for the flat log" "" "" 100
_long=$("$JB" log 2>/dev/null | grep '^12345678' | awk -F'\t' '{print $5}')
_len=${#_long}
if [ "$_len" -le 40 ]; then
    pass "jb log truncates subjects to 40 chars"
else
    fail "jb log truncates subjects to 40 chars" "got $_len chars: $_long"
fi

# ---- --graph: the committed forest ----
_gout=$("$JB" log --graph 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "jb log --graph exits 0"
else
    fail "jb log --graph exits 0" "got $_rc"
fi

# exact tree shape (fixtures are deterministic)
_exp="aaaaaaaa  root  \"root A subject\"
├── bbbbbbbb  fork  \"fork B subject\"
├── cccccccc  [error]  fork  \"fork C subject\"
└── ffffffff  fresh  \"fresh F subject\"
dddddddd  root  \"root D subject\"
└── eeeeeeee  fork  \"fork E subject\""
if [ "$_gout" = "$_exp" ]; then
    pass "jb log --graph renders the committed forest"
else
    fail "jb log --graph renders the committed forest" "got:
$_gout"
fi

# pending sessions are absent from the graph
case "$_gout" in
    *"gggggggg"*|*"deadbeef"*) fail "jb log --graph excludes pending sessions" ;;
    *) pass "jb log --graph excludes pending sessions" ;;
esac

# ---- edge cases ----
# no committed sessions → empty output, exit 0
new_scratch
repo_init
fixture_session "$_g" working "working G"
_gout=$("$JB" log --graph 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ] && [ -z "$_gout" ]; then
    pass "jb log --graph with nothing committed prints nothing"
else
    fail "jb log --graph with nothing committed prints nothing" "exit $_rc, out: $_gout"
fi
# flat log still shows the working session
if [ -n "$("$JB" log 2>/dev/null | grep '^gggggggg')" ]; then
    pass "jb log flat shows working sessions"
else
    fail "jb log flat shows working sessions"
fi

# outside a repo → exit 1
new_scratch
"$JB" log >/dev/null 2>&1
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "jb log outside a repo exits 1"
else
    fail "jb log outside a repo exits 1" "got $_rc"
fi
