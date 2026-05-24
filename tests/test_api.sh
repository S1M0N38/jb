# test_api.sh — Goal 4: API layer, first request

# jb should send a prompt and get a text response
# We ask a simple question and check stdout contains something
_out=$(echo "respond with exactly the word PONG and nothing else" | "$JB" 2>/dev/null)

case "$_out" in
    *"PONG"*) pass "jb returns model response on stdout" ;;
    *)        fail "jb returns model response on stdout" "got: $(echo "$_out" | head -c 200)" ;;
esac

# jb should exit 0 on success
_actual=$(echo "say hi" | "$JB" >/dev/null 2>/dev/null; echo $?)
if [ "$_actual" -eq 0 ]; then
    pass "jb exits 0 after successful API call"
else
    fail "jb exits 0 after successful API call" "got $_actual"
fi
