# test_path.sh — jb path ID: the bridge to raw session state (§7)
# stdout: the absolute session directory path; exit 0 · 1 not found

repo_init

# ---- fixture: one session with the three files ----
_uuid="21637177a1b2c3d4e5f60718293a4b5c6"
fixture_session "$_uuid" completed
_fixture_dir="$JB_SESSIONS_DIR/$_uuid"

# full uuid → the session dir, exit 0 (compare canonical: /var -> /private/var)
_fixture_dir=$(cd "$JB_SESSIONS_DIR/$_uuid" && pwd -P)
_dir=$("$JB" path "$_uuid" 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ] && [ "$_dir" = "$_fixture_dir" ]; then
    pass "jb path <full uuid> prints the session dir"
else
    fail "jb path <full uuid> prints the session dir" "got ($_rc): $_dir"
fi

# unique 4+ hex prefix resolves
_dir=$("$JB" path "2163" 2>/dev/null)
if [ "$_dir" = "$_fixture_dir" ]; then
    pass "jb path <unique prefix> resolves to the session dir"
else
    fail "jb path <unique prefix> resolves to the session dir" "got: $_dir"
fi

# @ = $JB_SESSION
_dir=$(JB_SESSION="$_uuid" "$JB" path @ 2>/dev/null)
if [ "$_dir" = "$_fixture_dir" ]; then
    pass "jb path @ resolves via JB_SESSION"
else
    fail "jb path @ resolves via JB_SESSION" "got: $_dir"
fi

# @ without $JB_SESSION → exit 1
env -u JB_SESSION "$JB" path @ >/dev/null 2>&1
if [ $? -eq 1 ]; then
    pass "jb path @ without JB_SESSION exits 1"
else
    fail "jb path @ without JB_SESSION exits 1" "exit $?"
fi

# unknown id → jb: no session 'xyz', exit 1
_err=$(env -u JB_SESSION "$JB" path xyz 2>&1 >/dev/null)
_rc=$?
case "$_err" in
    *"no session 'xyz'"*) pass "jb path <unknown> reports no session" ;;
    *) fail "jb path <unknown> reports no session" "stderr: $_err" ;;
esac
if [ "$_rc" -eq 1 ]; then
    pass "jb path <unknown> exits 1"
else
    fail "jb path <unknown> exits 1" "got $_rc"
fi

# ambiguous prefix → exit 1 with candidates
fixture_session "21637177a1b2c3d4e5f60718293a4b5c7" completed
_err=$(env -u JB_SESSION "$JB" path "2163" 2>&1 >/dev/null)
_rc=$?
case "$_err" in
    *ambiguous*) pass "jb path <ambiguous prefix> reports ambiguity" ;;
    *) fail "jb path <ambiguous prefix> reports ambiguity" "stderr: $_err" ;;
esac
if [ "$_rc" -eq 1 ]; then
    pass "jb path <ambiguous prefix> exits 1"
else
    fail "jb path <ambiguous prefix> exits 1" "got $_rc"
fi

# outside a repo → fatal, exit 1
new_scratch
"$JB" path "$_uuid" >/dev/null 2>&1
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "jb path outside a repo exits 1"
else
    fail "jb path outside a repo exits 1" "got $_rc"
fi

# compose: tail the conversation via jb path (the §7 idiom)
cd "$SCRATCH"   # back into the repo scratch (new_scratch moved us)
repo_init >/dev/null 2>&1 || true
if [ ! -d "$JB_SESSIONS_DIR/$_uuid" ]; then
    fixture_session "$_uuid" completed
fi
printf '{"type":"session","version":3,"id":"%s"}\n' "$_uuid" > "$JB_SESSIONS_DIR/$_uuid/session.jsonl"
_tail=$(tail -n 5 "$("$JB" path "$_uuid" 2>/dev/null)/session.jsonl")
case "$_tail" in
    *'"type":"session"'*) pass "tail -n 5 \"\$(jb path ID)/session.jsonl\" works" ;;
    *) fail "tail -n 5 \"\$(jb path ID)/session.jsonl\" works" "got: $_tail" ;;
esac

# compose: jq the metadata via jb path
_meta=$(jq -r .status "$("$JB" path "$_uuid" 2>/dev/null)/metadata.json" 2>/dev/null)
if [ "$_meta" = "completed" ]; then
    pass "jq . \"\$(jb path ID)/metadata.json\" works"
else
    fail "jq . \"\$(jb path ID)/metadata.json\" works" "got: $_meta"
fi
