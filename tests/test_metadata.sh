# test_metadata.sh — metadata.json: the jb index for a session (§6)

repo_init
prompt_pong | "$JB" run >/dev/null 2>&1
_latest=$(newest_session)
if [ -z "$_latest" ]; then
    fail "metadata.json checks" "no session dir found"
    return 0   # sourced by run.sh — return, never exit
fi
_meta="$_latest/metadata.json"
_uuid=$(basename "$_latest")

assert_file_exists "metadata.json created" "$_meta"

# Valid JSON
if jq empty "$_meta" >/dev/null 2>&1; then
    pass "metadata.json is valid JSON"
else
    fail "metadata.json is valid JSON" "$(cat "$_meta")"
fi

# uuid matches the session dir
_u=$(jq -r '.uuid // empty' "$_meta" 2>/dev/null)
if [ "$_u" = "$_uuid" ]; then
    pass "metadata uuid matches dir"
else
    fail "metadata uuid matches dir" "meta=$_u dir=$_uuid"
fi

# status: completed after a successful run (working at init — see lifecycle)
_s=$(jq -r '.status // empty' "$_meta" 2>/dev/null)
if [ "$_s" = "completed" ]; then
    pass "metadata status is completed"
else
    fail "metadata status is completed" "got: $_s"
fi

# subject = prompt first line
_sub=$(jq -r '.subject // empty' "$_meta" 2>/dev/null)
case "$_sub" in
    *PONG*) pass "subject is the prompt first line" ;;
    *)      fail "subject is the prompt first line" "got: $_sub" ;;
esac

# body is "" until commit
_b=$(jq -r '.body // empty' "$_meta" 2>/dev/null)
if [ -z "$_b" ]; then
    pass "body empty until commit"
else
    fail "body empty until commit" "got: $_b"
fi

# author is "" for a human run (no $JB_SESSION)
_a=$(jq -r '.author // empty' "$_meta" 2>/dev/null)
if [ -z "$_a" ]; then
    pass "author empty for human runs"
else
    fail "author empty for human runs" "got: $_a"
fi

# started_at / ended_at are ISO timestamps
case "$(jq -r '.started_at // empty' "$_meta" 2>/dev/null)" in
    *T*Z) pass "started_at is ISO timestamp" ;;
    *)    fail "started_at is ISO timestamp" "got: $(jq -r '.started_at' "$_meta" 2>/dev/null)" ;;
esac
case "$(jq -r '.ended_at // empty' "$_meta" 2>/dev/null)" in
    *T*Z) pass "ended_at is ISO timestamp" ;;
    *)    fail "ended_at is ISO timestamp" "got: $(jq -r '.ended_at' "$_meta" 2>/dev/null)" ;;
esac

# working_dir is the repo cwd (compare canonical: /var -> /private/var on macOS)
_wd=$(jq -r '.working_dir // empty' "$_meta" 2>/dev/null)
_phys=$(cd "$SCRATCH" && pwd -P)
if [ "$_wd" = "$_phys" ]; then
    pass "working_dir matches the run cwd"
else
    fail "working_dir matches the run cwd" "meta=$_wd cwd=$_phys"
fi

# config snapshot matches the real provider config
_real_model=$(jq -r '.model // empty' "$REAL_CFG" 2>/dev/null)
_meta_model=$(jq -r '.config.model // empty' "$_meta" 2>/dev/null)
if [ -n "$_real_model" ] && [ "$_meta_model" = "$_real_model" ]; then
    pass "config snapshot: model matches"
else
    fail "config snapshot: model matches" "expected $_real_model, got $_meta_model"
fi
for _k in api_url max_tokens max_output_lines max_output_bytes; do
    if jq -e ".config.$_k != null" "$_meta" >/dev/null 2>&1; then
        pass "config snapshot has $_k"
    else
        fail "config snapshot has $_k" "missing"
    fi
done

# counters from the close write
_t=$(jq -r '.turns // empty' "$_meta" 2>/dev/null)
if [ "$_t" -ge 1 ] 2>/dev/null; then
    pass "turns recorded (>= 1)"
else
    fail "turns recorded (>= 1)" "got: $_t"
fi
_tok=$(jq -r '.tokens_used // empty' "$_meta" 2>/dev/null)
if [ -n "$_tok" ]; then
    pass "tokens_used recorded"
else
    fail "tokens_used recorded" "missing"
fi
_ec=$(jq -r '.exit_code // empty' "$_meta" 2>/dev/null)
if [ "$_ec" = "0" ]; then
    pass "exit_code 0 recorded"
else
    fail "exit_code 0 recorded" "got: $_ec"
fi

# last_activity present (heartbeat field)
_la=$(jq -r '.last_activity // empty' "$_meta" 2>/dev/null)
if [ -n "$_la" ]; then
    pass "last_activity present"
else
    fail "last_activity present" "missing"
fi
