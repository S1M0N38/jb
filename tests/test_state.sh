# test_state.sh — Goal 7: session state persistence

# Run jb and check that state.jsonl has content
_out=$(echo "say hello" | "$JB" 2>/dev/null)

# Find latest session
_cache_dir="${XDG_CACHE_HOME:-$HOME/.cache}/jb/sessions"
_latest=$(ls -td "$_cache_dir"/*/ 2>/dev/null | head -1)

if [ -n "$_latest" ]; then
    _lines=$(wc -l < "$_latest/state.jsonl")
    if [ "$_lines" -ge 2 ]; then
        pass "state.jsonl has multiple messages ($_lines lines)"
    else
        fail "state.jsonl has multiple messages" "only $_lines lines"
    fi

    # Check that state.jsonl starts with system message
    _first=$(head -1 "$_latest/state.jsonl")
    case "$_first" in
        *system*) pass "state.jsonl starts with system message" ;;
        *)        fail "state.jsonl starts with system message" "got: $(echo "$_first" | head -c 100)" ;;
    esac

    # Check that log.jsonl has SSE events
    _log_lines=$(wc -l < "$_latest/log.jsonl")
    if [ "$_log_lines" -ge 1 ]; then
        pass "log.jsonl has SSE events ($_log_lines lines)"
    else
        fail "log.jsonl has SSE events" "$_log_lines lines"
    fi

    _first_log=$(head -1 "$_latest/log.jsonl")
    case "$_first_log" in
        *data:*) pass "log.jsonl contains SSE data lines" ;;
        *)       fail "log.jsonl contains SSE data lines" ;;
    esac
else
    fail "state.jsonl checks" "no session dir found"
fi
