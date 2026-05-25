# test_final_answer_state.sh — final assistant answer is persisted to state.jsonl

# Use a prompt that produces a direct text answer (no tool calls)
# so we hit the finish_reason="stop" path
_out=$(echo "say hello" | "$JB" 2>/dev/null) || true

# Find latest session
_cache_dir="${XDG_CACHE_HOME:-$HOME/.cache}/jb/sessions"
_latest=$(ls -td "$_cache_dir"/*/ 2>/dev/null | head -1)

if [ -z "$_latest" ]; then
    fail "final answer in state.jsonl" "no session dir found"
    exit 0
fi

_state="$_latest/state.jsonl"

# The final line of state.jsonl should be an assistant message with content (the answer)
_last_line=$(tail -1 "$_state")

# Must have role=assistant
case "$_last_line" in
    *"role"*"assistant"*) pass "last state line is assistant role" ;;
    *) fail "last state line is assistant role" "got: $(echo "$_last_line" | head -c 200)" ;;
esac

# Must have non-null content (the model's final text answer)
_has_content=$(printf '%s' "$_last_line" | jq 'has("content")' 2>/dev/null)
if [ "$_has_content" = "true" ]; then
    _content_val=$(printf '%s' "$_last_line" | jq -r '.content' 2>/dev/null)
    if [ -n "$_content_val" ] && [ "$_content_val" != "null" ]; then
        pass "final assistant message has non-null content"
    else
        fail "final assistant message has non-null content" "content is null/empty"
    fi
else
    fail "final assistant message has content field" "no content key in: $(echo "$_last_line" | head -c 200)"
fi
