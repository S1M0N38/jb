# test_show.sh — jb show [ID]: pretty-print session metadata (§7)
# stdout: metadata.json, pretty-printed (indent 2) · stderr: the metadata path
# ID defaults to @ when $JB_SESSION is set

repo_init

_uuid="21637177a1b2c3d4e5f60718293a4b5c6"
fixture_session "$_uuid" completed "the fixture subject"
_meta_path="$JB_SESSIONS_DIR/$_uuid/metadata.json"

# full uuid → valid JSON with the fixture's data, exit 0
_out=$("$JB" show "$_uuid" 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ] && printf '%s' "$_out" | jq empty >/dev/null 2>&1; then
    pass "jb show <id> prints valid JSON"
else
    fail "jb show <id> prints valid JSON" "exit $_rc, out: $(echo "$_out" | head -c 120)"
fi
_st=$(printf '%s' "$_out" | jq -r '.status' 2>/dev/null)
_su=$(printf '%s' "$_out" | jq -r '.subject' 2>/dev/null)
if [ "$_st" = "completed" ] && [ "$_su" = "the fixture subject" ]; then
    pass "jb show <id> prints the fixture metadata (untouched)"
else
    fail "jb show <id> prints the fixture metadata (untouched)" "status=$_st subject=$_su"
fi

# pretty: first line "{", top-level keys indented two spaces (indent 2)
_first=$(printf '%s\n' "$_out" | sed -n '1p')
_second=$(printf '%s\n' "$_out" | sed -n '2p')
case "$_first" in
    '{') pass "jb show pretty-prints (opens with {)" ;;
    *) fail "jb show pretty-prints (opens with {)" "got: $_first" ;;
esac
case "$_second" in
    '  "'*) pass "jb show indents two spaces" ;;
    *) fail "jb show indents two spaces" "got: $(echo "$_second" | head -c 40)" ;;
esac

# stderr carries the metadata path (canonical: /var -> /private/var)
_meta_path=$(cd "$JB_SESSIONS_DIR/$_uuid" && pwd -P)/metadata.json
_err=$("$JB" show "$_uuid" 2>&1 >/dev/null)
case "$_err" in
    *"jb: metadata: $_meta_path"*) pass "jb show prints the metadata path to stderr" ;;
    *) fail "jb show prints the metadata path to stderr" "got: $_err" ;;
esac

# ID defaults to @ when $JB_SESSION is set
_out=$(JB_SESSION="$_uuid" "$JB" show 2>/dev/null)
if [ "$(printf '%s' "$_out" | jq -r .uuid 2>/dev/null)" = "$_uuid" ]; then
    pass "jb show defaults to @ when JB_SESSION is set"
else
    fail "jb show defaults to @ when JB_SESSION is set" "got: $(echo "$_out" | head -c 80)"
fi

# explicit @
_out=$(JB_SESSION="$_uuid" "$JB" show @ 2>/dev/null)
if [ "$(printf '%s' "$_out" | jq -r .uuid 2>/dev/null)" = "$_uuid" ]; then
    pass "jb show @ resolves via JB_SESSION"
else
    fail "jb show @ resolves via JB_SESSION" "got: $(echo "$_out" | head -c 80)"
fi

# no ID, no $JB_SESSION → exit 1
env -u JB_SESSION "$JB" show >/dev/null 2>&1
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "jb show without ID or JB_SESSION exits 1"
else
    fail "jb show without ID or JB_SESSION exits 1" "got $_rc"
fi

# unknown id → exit 1
env -u JB_SESSION "$JB" show xyz >/dev/null 2>&1
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "jb show <unknown> exits 1"
else
    fail "jb show <unknown> exits 1" "got $_rc"
fi

# the §7 idiom: jb show <id> | jq .status — stuck-child detection
if [ "$("$JB" show "$_uuid" 2>/dev/null | jq -r .last_activity 2>/dev/null)" != "" ]; then
    pass "jb show | jq .last_activity works (stuck detection)"
else
    fail "jb show | jq .last_activity works (stuck detection)"
fi

# outside a repo → fatal, exit 1
new_scratch
"$JB" show "$_uuid" >/dev/null 2>&1
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "jb show outside a repo exits 1"
else
    fail "jb show outside a repo exits 1" "got $_rc"
fi
