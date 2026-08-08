# test_fork.sh — --fork: reader + trim, history replay, parent lineage
# (implementation plan phase 4: load session.jsonl, convert to wire, trim
# dangling tail; fork's session.jsonl contains the parent's history; header
# parentSession; metadata parent set)

repo_init

# ======================================================================
# Slice 1: ID resolution errors (no API calls, crafted dirs only)
# ======================================================================

# Two crafted session dirs sharing a 4-hex prefix → ambiguous
mkdir -p "$JB_SESSIONS_DIR/abc12345-0000-4000-8000-000000000001"
mkdir -p "$JB_SESSIONS_DIR/abc1ffff-0000-4000-8000-000000000002"

echo "hi" | "$JB" run --fork abc1 >/dev/null 2>"$SCRATCH/amb.err"
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "--fork ambiguous prefix exits 1"
else
    fail "--fork ambiguous prefix exits 1" "got exit $_rc"
fi
_amb=$(cat "$SCRATCH/amb.err")
case "$_amb" in
    *"jb: ambiguous id 'abc1'"*"abc12345"*"abc1ffff"*) pass "--fork ambiguous lists candidates" ;;
    *) fail "--fork ambiguous lists candidates" "got: $_amb" ;;
esac

# Unresolvable ID
echo "hi" | "$JB" run --fork deadbeef00 >/dev/null 2>"$SCRATCH/nosess.err"
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "--fork unknown session exits 1"
else
    fail "--fork unknown session exits 1" "got exit $_rc"
fi
case "$(cat "$SCRATCH/nosess.err")" in
    *"jb: no session 'deadbeef00'"*) pass "--fork unknown session error message" ;;
    *) fail "--fork unknown session error message" "got: $(cat "$SCRATCH/nosess.err")" ;;
esac

# --fork @ without $JB_SESSION
echo "hi" | env -u JB_SESSION "$JB" run --fork @ >/dev/null 2>"$SCRATCH/at.err"
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "--fork @ without env exits 1"
else
    fail "--fork @ without env exits 1" "got exit $_rc"
fi
case "$(cat "$SCRATCH/at.err")" in
    *"jb: JB_SESSION not set"*) pass "--fork @ without env error message" ;;
    *) fail "--fork @ without env error message" "got: $(cat "$SCRATCH/at.err")" ;;
esac

# Session dir exists but session.jsonl is missing/invalid
mkdir -p "$JB_SESSIONS_DIR/c0ffee00-0000-4000-8000-0000000000aa"
echo "hi" | "$JB" run --fork c0ffee00 >/dev/null 2>"$SCRATCH/badload.err"
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "--fork with missing session.jsonl exits 1"
else
    fail "--fork with missing session.jsonl exits 1" "got exit $_rc"
fi
case "$(cat "$SCRATCH/badload.err")" in
    *"jb: --fork: cannot load session"*) pass "--fork load failure error message" ;;
    *) fail "--fork load failure error message" "got: $(cat "$SCRATCH/badload.err")" ;;
esac

# An empty session.jsonl is not a valid pi session (no v3 header)
mkdir -p "$JB_SESSIONS_DIR/e5e5e5e5-0000-4000-8000-0000000000bb"
: > "$JB_SESSIONS_DIR/e5e5e5e5-0000-4000-8000-0000000000bb/session.jsonl"
echo "hi" | "$JB" run --fork e5e5e5e5 >/dev/null 2>"$SCRATCH/empty.err"
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "--fork with empty session.jsonl exits 1"
else
    fail "--fork with empty session.jsonl exits 1" "got exit $_rc"
fi
case "$(cat "$SCRATCH/empty.err")" in
    *"jb: --fork: cannot load session"*) pass "--fork empty session.jsonl error message" ;;
    *) fail "--fork empty session.jsonl error message" "got: $(cat "$SCRATCH/empty.err")" ;;
esac

# ======================================================================
# Slice 2: reader + trim — fixture parent with a dangling tail
# ======================================================================

# Fixture: user → assistant(toolUse call_1) → toolResult call_1 (complete
# turn), then a dangling tail: assistant(toolUse call_2) WITHOUT its result
# and an unpaired toolResult (call_3). The fork must keep only the complete
# turn.
_fixture="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"
mkdir -p "$JB_SESSIONS_DIR/$_fixture"
_fixture_sj="$JB_SESSIONS_DIR/$_fixture/session.jsonl"
cat > "$_fixture_sj" <<'EOF'
{"type":"session","version":3,"id":"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee","timestamp":"2026-08-06T00:00:00.000Z","cwd":"/tmp"}
{"type":"message","id":"00000001","parentId":null,"timestamp":"2026-08-06T00:00:00.001Z","message":{"role":"user","content":[{"type":"text","text":"first prompt"}],"timestamp":1}}
{"type":"message","id":"00000002","parentId":"00000001","timestamp":"2026-08-06T00:00:00.002Z","message":{"role":"assistant","content":[{"type":"toolCall","id":"call_1","name":"read","arguments":{"path":"src/jb.c"}}],"api":"openai-completions","provider":"local","model":"test-model","usage":{"input":10,"output":5,"cacheRead":0,"cacheWrite":0,"reasoning":0,"totalTokens":15,"cost":{"input":0,"output":0,"cacheRead":0,"cacheWrite":0,"total":0}},"stopReason":"toolUse","timestamp":2}}
{"type":"message","id":"00000003","parentId":"00000002","timestamp":"2026-08-06T00:00:00.003Z","message":{"role":"toolResult","toolCallId":"call_1","toolName":"read","content":[{"type":"text","text":"file contents"}],"isError":false,"timestamp":3}}
{"type":"message","id":"00000004","parentId":"00000003","timestamp":"2026-08-06T00:00:00.004Z","message":{"role":"assistant","content":[{"type":"toolCall","id":"call_2","name":"bash","arguments":{"command":"echo hi"}}],"api":"openai-completions","provider":"local","model":"test-model","usage":{"input":1,"output":1,"cacheRead":0,"cacheWrite":0,"reasoning":0,"totalTokens":2,"cost":{"input":0,"output":0,"cacheRead":0,"cacheWrite":0,"total":0}},"stopReason":"toolUse","timestamp":4}}
{"type":"message","id":"00000005","parentId":"00000004","timestamp":"2026-08-06T00:00:00.005Z","message":{"role":"toolResult","toolCallId":"call_3","toolName":"bash","content":[{"type":"text","text":"hi"}],"isError":false,"timestamp":5}}
EOF

_fork_out=$(echo "reply with exactly the word PONG" | "$JB" run --fork "$_fixture" 2>"$SCRATCH/fork.err")
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "fork of fixture runs"
else
    fail "fork of fixture runs" "exit $_rc: $(cat "$SCRATCH/fork.err")"
fi
case "$_fork_out" in
    *PONG*) pass "fork answers" ;;
    *)      fail "fork answers" "got: $(echo "$_fork_out" | head -c 120)" ;;
esac

_fork=$(newest_session)
# Guard: a failed run must not fall back to a crafted fixture dir
if [ -n "$_fork" ] && [ "$_fork" != "$JB_SESSIONS_DIR/$_fixture/" ]; then
    pass "fork session dir exists (not a fixture)"
else
    fail "fork session dir exists (not a fixture)" "got: $_fork"
fi
_sj="$_fork/session.jsonl"

# Header carries parentSession = the parent's session.jsonl path
# (match the suffix: getcwd resolves symlinks — /var → /private/var — so
# the absolute prefix may differ from $SCRATCH's spelling)
_hps=$(head -1 "$_sj" | jq -r '.parentSession // empty' 2>/dev/null)
case "$_hps" in
    *"/.jb/sessions/$_fixture/session.jsonl") pass "header parentSession points at the parent session.jsonl" ;;
    *) fail "header parentSession points at the parent session.jsonl" "got: $_hps" ;;
esac

# Metadata: parent set, author empty (human fork)
_mp=$(jq -r '.parent // empty' "$_fork/metadata.json" 2>/dev/null)
if [ "$_mp" = "$_fixture" ]; then
    pass "metadata parent is the fixture uuid"
else
    fail "metadata parent is the fixture uuid" "got: $_mp"
fi
_ma=$(jq -r '.author // empty' "$_fork/metadata.json" 2>/dev/null)
if [ -z "$_ma" ]; then
    pass "fork metadata author empty (human fork)"
else
    fail "fork metadata author empty (human fork)" "got: $_ma"
fi

# The complete turn is replayed byte-identically (message objects)
_replay_ok=1
_replay_err=""
_i=2
while [ "$_i" -le 4 ]; do
    _a=$(sed -n "${_i}p" "$_sj" | jq -S '.message' 2>/dev/null)
    _b=$(sed -n "${_i}p" "$_fixture_sj" | jq -S '.message' 2>/dev/null)
    if [ "$_a" != "$_b" ]; then
        _replay_ok=0
        _replay_err="line $_i differs"
    fi
    _i=$((_i + 1))
done
if [ "$_replay_ok" -eq 1 ]; then
    pass "parent history replayed byte-identically (3 entries)"
else
    fail "parent history replayed byte-identically (3 entries)" "$_replay_err"
fi

# The dangling tail is excluded: no call_2 (unanswered toolCall), no call_3
# (unpaired toolResult)
if ! grep -q 'call_2' "$_sj" && ! grep -q 'call_3' "$_sj"; then
    pass "dangling tail excluded from the fork"
else
    fail "dangling tail excluded from the fork" "$(grep -c 'call_[23]' "$_sj") tail refs remain"
fi

# The fork's own user entry follows the replayed history
if grep -q 'reply with exactly the word PONG' "$_sj"; then
    pass "fork user entry present after history"
else
    fail "fork user entry present after history" "missing"
fi

# Entries form a parentId chain (null first) across the replay boundary
_chain_ok=1
_prev_id=""
_lineno=0
while IFS= read -r _line; do
    _lineno=$((_lineno + 1))
    [ "$_lineno" -eq 1 ] && continue   # header
    _epid=$(printf '%s' "$_line" | jq -r '.parentId' 2>/dev/null)
    _expect="null"
    [ "$_lineno" -gt 2 ] && _expect="$_prev_id"
    if [ "$_epid" != "$_expect" ]; then
        _chain_ok=0
        _chain_err="line $_lineno parentId=$_epid expected=$_expect"
    fi
    _prev_id=$(printf '%s' "$_line" | jq -r '.id // empty' 2>/dev/null)
done < "$_sj"
if [ "$_chain_ok" -eq 1 ]; then
    pass "fork entries form a parentId chain (null first)"
else
    fail "fork entries form a parentId chain (null first)" "$_chain_err"
fi

# ======================================================================
# Slice 3: reader — parent with NO complete assistant message: fork keeps
# no history (an interrupted session never leaks into the fork)
# ======================================================================

_none="bbbbbbbb-cccc-4ddd-8eee-ffffffffffff"
mkdir -p "$JB_SESSIONS_DIR/$_none"
cat > "$JB_SESSIONS_DIR/$_none/session.jsonl" <<'EOF'
{"type":"session","version":3,"id":"bbbbbbbb-cccc-4ddd-8eee-ffffffffffff","timestamp":"2026-08-06T00:00:00.000Z","cwd":"/tmp"}
{"type":"message","id":"10000001","parentId":null,"timestamp":"2026-08-06T00:00:00.001Z","message":{"role":"user","content":[{"type":"text","text":"second prompt"}],"timestamp":1}}
{"type":"message","id":"10000002","parentId":"10000001","timestamp":"2026-08-06T00:00:00.002Z","message":{"role":"assistant","content":[{"type":"text","text":"partial answer"}],"api":"openai-completions","provider":"local","model":"test-model","usage":{"input":5,"output":2,"cacheRead":0,"cacheWrite":0,"reasoning":0,"totalTokens":7,"cost":{"input":0,"output":0,"cacheRead":0,"cacheWrite":0,"total":0}},"stopReason":"aborted","errorMessage":"interrupted by signal 2","timestamp":2}}
EOF

echo "reply with exactly the word PONG" | "$JB" run --fork "$_none" >/dev/null 2>"$SCRATCH/nfork.err"
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "fork of interrupted fixture runs"
else
    fail "fork of interrupted fixture runs" "exit $_rc: $(cat "$SCRATCH/nfork.err")"
fi
_nfork=$(newest_session)
if [ -n "$_nfork" ] && [ "$_nfork" != "$JB_SESSIONS_DIR/$_none/" ] && [ "$_nfork" != "$_fork" ]; then
    pass "interrupted-fork session dir exists (not a fixture)"
else
    fail "interrupted-fork session dir exists (not a fixture)" "got: $_nfork"
fi
_nsj="$_nfork/session.jsonl"

# The interrupted parent's content is absent
if grep -q 'second prompt' "$_nsj" && grep -q 'partial answer' "$_nsj"; then
    fail "interrupted history not leaked into fork" "parent content present"
else
    pass "interrupted history not leaked into fork"
fi

# The fork starts fresh with its own user entry (header + user + reply)
_nu=$(sed -n '2p' "$_nsj" | jq -r '.message.role // empty' 2>/dev/null)
_nut=$(sed -n '2p' "$_nsj" | jq -r '.message.content[0].text // empty' 2>/dev/null)
if [ "$_nu" = "user" ] && [ "$_nut" = "reply with exactly the word PONG" ]; then
    pass "fork starts with its own user entry"
else
    fail "fork starts with its own user entry" "role=$_nu text=$_nut"
fi

# ======================================================================
# Slice 4: fork a real session — history copied, lineage recorded
# ======================================================================

echo "reply with exactly the word PONG" | "$JB" run >/dev/null 2>&1
_src=$(newest_session)
_src_uuid=$(basename "$_src")
_src_lines=$(wc -l < "$_src/session.jsonl" | tr -d ' ')
_src8=$(printf '%s' "$_src_uuid" | cut -c1-8)

_fork2_out=$(echo "reply with exactly the word FROG" | "$JB" run --fork "$_src_uuid" 2>"$SCRATCH/fork2.err")
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "fork of real session runs"
else
    fail "fork of real session runs" "exit $_rc: $(cat "$SCRATCH/fork2.err")"
fi
case "$_fork2_out" in
    *FROG*) pass "fork of real session answers" ;;
    *)      fail "fork of real session answers" "got: $(echo "$_fork2_out" | head -c 120)" ;;
esac

# Banner names the parent: "jb: session <8> started (from <src8>)"
case "$(cat "$SCRATCH/fork2.err")" in
    *"jb: session "*" started (from $_src8)"*) pass "fork banner shows parent" ;;
    *) fail "fork banner shows parent" "got: $(cat "$SCRATCH/fork2.err")" ;;
esac

# Find the fork via metadata.parent
_fork2=""
for _d in "$JB_SESSIONS_DIR"/*/; do
    [ -f "${_d}metadata.json" ] || continue
    _p=$(jq -r '.parent // empty' "${_d}metadata.json" 2>/dev/null)
    if [ "$_p" = "$_src_uuid" ]; then
        _fork2="$_d"
    fi
done
if [ -n "$_fork2" ]; then
    pass "fork found via metadata.parent"
else
    fail "fork found via metadata.parent" "no session with parent=$_src_uuid"
fi

# The fork's session.jsonl = parent history + the fork's own user + reply
_f2_lines=$(wc -l < "$_fork2/session.jsonl" | tr -d ' ')
_min=$((_src_lines + 2))
if [ "$_f2_lines" -ge "$_min" ]; then
    pass "fork carries the parent's full history" "fork=$_f2_lines lines, src=$_src_lines (+2)"
else
    fail "fork carries the parent's full history" "fork=$_f2_lines, expected >= $_min"
fi

# The parent's prompt text appears in the fork's replayed history
if grep -q 'reply with exactly the word PONG' "$_fork2/session.jsonl"; then
    pass "parent prompt replayed in fork"
else
    fail "parent prompt replayed in fork" "missing"
fi

# ======================================================================
# Slice 5: --fork @ — the $JB_SESSION session is the parent
# ======================================================================

_fork3_out=$(echo "reply with exactly the word FROG" | env JB_SESSION="$_src_uuid" "$JB" run --fork @ 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "--fork @ runs"
else
    fail "--fork @ runs" "exit $_rc"
fi
case "$_fork3_out" in
    *FROG*) pass "--fork @ answers" ;;
    *)      fail "--fork @ answers" "got: $(echo "$_fork3_out" | head -c 120)" ;;
esac

_fork3=$(newest_session)
if [ -n "$_fork3" ] && [ "$_fork3" != "$_fork2" ]; then
    pass "--fork @ creates a distinct session"
else
    fail "--fork @ creates a distinct session" "got: $_fork3"
fi
_f3p=$(jq -r '.parent // empty' "$_fork3/metadata.json" 2>/dev/null)
_f3a=$(jq -r '.author // empty' "$_fork3/metadata.json" 2>/dev/null)
if [ "$_f3p" = "$_src_uuid" ] && [ "$_f3a" = "$_src_uuid" ]; then
    pass "--fork @ sets parent and author to the env session"
else
    fail "--fork @ sets parent and author to the env session" "parent=$_f3p author=$_f3a"
fi
