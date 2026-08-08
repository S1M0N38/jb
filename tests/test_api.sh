# test_api.sh — end-to-end: real API run over the new CLI

repo_init

# jb run sends the prompt and streams the answer to stdout
_out=$(prompt_pong | "$JB" run 2>/dev/null)
case "$_out" in
    *"PONG"*) pass "jb run returns model response on stdout" ;;
    *)        fail "jb run returns model response on stdout" "got: $(echo "$_out" | head -c 200)" ;;
esac

# jb run exits 0 on success
_actual=$(prompt_pong | "$JB" run >/dev/null 2>/dev/null; echo $?)
if [ "$_actual" -eq 0 ]; then
    pass "jb run exits 0 after successful API call"
else
    fail "jb run exits 0 after successful API call" "got $_actual"
fi

# The session record: metadata completed, header + user entry recorded
_latest=$(newest_session)
if [ -z "$_latest" ]; then
    fail "e2e session record" "no session dir found"
    return 0
fi
_s=$(jq -r '.status // empty' "$_latest/metadata.json" 2>/dev/null)
if [ "$_s" = "completed" ]; then
    pass "e2e: metadata working → completed"
else
    fail "e2e: metadata working → completed" "got: $_s"
fi
_head=$(head -1 "$_latest/session.jsonl")
case "$_head" in
    *'"type":"session"'*) pass "e2e: v3 header recorded" ;;
    *)                    fail "e2e: v3 header recorded" "got: $(echo "$_head" | head -c 100)" ;;
esac
_uentry=$(sed -n '2p' "$_latest/session.jsonl")
case "$_uentry" in
    *'"role":"user"'*) pass "e2e: user message recorded" ;;
    *)                 fail "e2e: user message recorded" "got: $(echo "$_uentry" | head -c 100)" ;;
esac
_atail=$(tail -1 "$_latest/session.jsonl")
case "$_atail" in
    *'"role":"assistant"'*) pass "e2e: assistant answer recorded" ;;
    *)                        fail "e2e: assistant answer recorded" "got: $(echo "$_atail" | head -c 100)" ;;
esac
