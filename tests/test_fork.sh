# test_fork.sh — --fork flag: continue a session with full history

_cache_dir="${XDG_CACHE_HOME:-$HOME/.cache}/jb/sessions"

# --- Slice 1: --fork loads source history and sets strong parent ---

# Create a source session first (non-agentic prompt so the model doesn't spawn children)
_src_out=$(echo "reply with exactly the word PONG" | "$JB" 2>/dev/null)
_rc=$?
if [ "$_rc" -ne 0 ]; then
    fail "source session runs" "exit code $_rc"
else
    pass "source session runs"
fi

# Find the newest session (the source)
_src_dir=$(ls -td "$_cache_dir"/*/ 2>/dev/null | head -1)
if [ -z "$_src_dir" ] || [ ! -f "$_src_dir/metadata.json" ]; then
    fail "fork: source session dir found" "no session dir"
else
    pass "fork: source session dir found"
fi

_src_uuid=$(jq -r '.uuid' "$_src_dir/metadata.json" 2>/dev/null)
_src_msgs=$(wc -l < "$_src_dir/state.jsonl" 2>/dev/null)
_src_msgs=${_src_msgs:-0}

# Fork it
_fork_out=$(echo "reply with exactly the word FROG" | "$JB" --fork "$_src_uuid" 2>/dev/null)
_rc2=$?
if [ "$_rc2" -ne 0 ]; then
    fail "--fork runs successfully" "exit code $_rc2"
else
    case "$_fork_out" in
        *FROG*) pass "--fork runs successfully and answers" ;;
        *)      fail "--fork runs successfully and answers" "got: $(echo "$_fork_out" | head -c 200)" ;;
    esac
fi

# Find the fork deterministically: the session whose parent == source uuid
_fork_dir=""
for _d in "$_cache_dir"/*/; do
    [ -f "${_d}metadata.json" ] || continue
    _p=$(jq -r '.parent // empty' "${_d}metadata.json" 2>/dev/null)
    if [ "$_p" = "$_src_uuid" ]; then
        _fork_dir="$_d"
        break
    fi
done

if [ -z "$_fork_dir" ]; then
    fail "fork: fork session found by parent" "no session with parent=$_src_uuid"
else
    pass "fork: fork session found by parent"
fi

# --- Check strong link (parent) ---
_fork_parent=$(jq -r '.parent // empty' "$_fork_dir/metadata.json" 2>/dev/null)
if [ "$_fork_parent" = "$_src_uuid" ]; then
    pass "--fork sets parent (strong link) to source"
else
    fail "--fork sets parent (strong link) to source" "expected $_src_uuid, got: $_fork_parent"
fi

# --- Check history was copied: fork state has >= source messages + 1 new user msg ---
_fork_msgs=$(wc -l < "$_fork_dir/state.jsonl" 2>/dev/null)
_fork_msgs=${_fork_msgs:-0}
_expected=$((_src_msgs + 1))
if [ "$_fork_msgs" -ge "$_expected" ]; then
    pass "--fork copies full history" "source=$_src_msgs, fork=$_fork_msgs (>= $_expected)"
else
    fail "--fork copies full history" "source=$_src_msgs msgs, fork=$_fork_msgs, expected >= $_expected"
fi

# --- Check first line of fork state equals first line of source state (system msg) ---
_src_first=$(head -1 "$_src_dir/state.jsonl" 2>/dev/null)
_fork_first=$(head -1 "$_fork_dir/state.jsonl" 2>/dev/null)
if [ "$_src_first" = "$_fork_first" ]; then
    pass "--fork preserves system message (stale by design)"
else
    fail "--fork preserves system message" "fork first line differs from source"
fi

# --- Slice 2: --fork with nonexistent session → exit 3 ---
_nonexistent="00000000-0000-0000-0000-000000000000"
echo "hi" | "$JB" --fork "$_nonexistent" >/dev/null 2>&1
_rc3=$?
if [ "$_rc3" -eq 3 ]; then
    pass "--fork with missing session exits 3"
else
    fail "--fork with missing session exits 3" "got $_rc3"
fi

# --- Slice 3: fork of fork (chained conversation) ---
_chain_out=$(echo "reply with exactly the word CHAIN" | "$JB" --fork "$_src_uuid" 2>/dev/null)
_rc4=$?
if [ "$_rc4" -eq 0 ]; then
    pass "fork-of-fork runs"
else
    fail "fork-of-fork runs" "exit $_rc4"
fi

# The second fork should be a sibling (same parent, different uuid)
_fork2_dir=""
for _d in "$_cache_dir"/*/; do
    [ -f "${_d}metadata.json" ] || continue
    _p=$(jq -r '.parent // empty' "${_d}metadata.json" 2>/dev/null)
    _u=$(jq -r '.uuid' "${_d}metadata.json" 2>/dev/null)
    if [ "$_p" = "$_src_uuid" ] && [ "$_u" != "$(jq -r '.uuid' "$_fork_dir/metadata.json" 2>/dev/null)" ]; then
        _fork2_dir="$_d"
        break
    fi
done
if [ -n "$_fork2_dir" ]; then
    pass "second fork is a sibling (same parent, distinct uuid)"
else
    fail "second fork is a sibling" "no distinct fork found"
fi
