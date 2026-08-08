# test_repo.sh — repository resolution: fatal outside a repo, -C DIR

# ---- Outside any repo: verbs are fatal ----

# A dir with no .jb anywhere above it (under the scratch's tmp)
_outside=$(mktemp -d "$TMPDIR/jb-outside.XXXXXX")

# jb run outside a repo → fatal, exit 1
_err=$(cd "$_outside" && echo "hi" | "$JB" run 2>&1 >/dev/null)
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "jb run outside a repo exits 1"
else
    fail "jb run outside a repo exits 1" "got $_rc"
fi
case "$_err" in
    *"fatal: not a jb repository"*"jb init"*) pass "jb run outside a repo prints fatal" ;;
    *)                                       fail "jb run outside a repo prints fatal" "got: $_err" ;;
esac

# jb run outside a repo fails before reading stdin (no API call, no session)
_n_sessions=$(find "$_outside" -name metadata.json 2>/dev/null | wc -l | tr -d ' ')
if [ "$_n_sessions" -eq 0 ]; then
    pass "jb run outside a repo creates nothing"
else
    fail "jb run outside a repo creates nothing" "$_n_sessions metadata files found"
fi

# ---- -C DIR resolves the repository from DIR ----

# Repo in the scratch; run init with -C from outside the repo
"$JB" init >/dev/null 2>&1
_rc2=$?
if [ "$_rc2" -eq 0 ]; then
    pass "setup: jb init in scratch"
else
    fail "setup: jb init in scratch" "got $_rc2"
fi

# init with -C from a different cwd reinitializes the scratch repo
_cerr=$(cd "$_outside" && "$JB" -C "$SCRATCH" init 2>&1)
_rc3=$?
if [ "$_rc3" -eq 0 ]; then
    pass "-C DIR: init from outside exits 0"
else
    fail "-C DIR: init from outside exits 0" "got $_rc3"
fi
case "$_cerr" in
    *"reinitialized"*) pass "-C DIR resolves the repo dir" ;;
    *)                 fail "-C DIR resolves the repo dir" "got: $_cerr" ;;
esac

# run resolves from -C DIR (repo in scratch, cwd outside)
_out=$(cd "$_outside" && prompt_pong | "$JB" -C "$SCRATCH" run 2>/dev/null)
_rc4=$?
if [ "$_rc4" -eq 0 ]; then
    pass "-C DIR: run from outside exits 0"
else
    fail "-C DIR: run from outside exits 0" "got $_rc4"
fi
case "$_out" in
    *PONG*) pass "-C DIR: run resolves the repo and answers" ;;
    *)      fail "-C DIR: run resolves the repo and answers" "got: $(echo "$_out" | head -c 200)" ;;
esac

# The session landed in the -C repo (slice C asserts .jb/sessions details)
# Outside dir stays untouched
if find "$_outside" -name metadata.json 2>/dev/null | grep -q .; then
    fail "-C DIR: nothing written outside the repo" "found metadata.json under $_outside"
else
    pass "-C DIR: nothing written outside the repo"
fi

# -C to a nonexistent dir → fatal, exit 1
_nerr=$(cd "$_outside" && "$JB" -C "$_outside/nonexistent-dir" init 2>&1 >/dev/null)
_rc5=$?
if [ "$_rc5" -eq 1 ]; then
    pass "-C nonexistent dir exits 1"
else
    fail "-C nonexistent dir exits 1" "got $_rc5"
fi
case "$_nerr" in
    *"cannot change to"*) pass "-C nonexistent dir prints fatal" ;;
    *)                    fail "-C nonexistent dir prints fatal" "got: $_nerr" ;;
esac

# ---- Walk-up: a subdir of a repo resolves the same repo ----

mkdir -p "$SCRATCH/sub/deep"
_out2=$(cd "$SCRATCH/sub/deep" && prompt_pong | "$JB" run 2>/dev/null)
_rc6=$?
if [ "$_rc6" -eq 0 ]; then
    pass "subdir walk-up: run exits 0"
else
    fail "subdir walk-up: run exits 0" "got $_rc6"
fi
case "$_out2" in
    *PONG*) pass "subdir walk-up: run answers" ;;
    *)      fail "subdir walk-up: run answers" "got: $(echo "$_out2" | head -c 200)" ;;
esac

rm -rf "$_outside"
