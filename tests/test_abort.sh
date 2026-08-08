# test_abort.sh — SIGINT mid-run: aborted entry + interrupted metadata (phase 3)

repo_init

# ---- Test A (deterministic, no API): SIGINT while stdin is still open ----
# jb blocks reading the prompt; the session exists but nothing was written
# yet. SIGINT must still mark the session interrupted and exit 130.

sleep 30 | "$JB" run >/dev/null 2>&1 &
_jbpid=$!
# wait for startup (session dir exists) so the signal handler is installed
_i=0
while [ -z "$(newest_session)" ] && [ "$_i" -lt 50 ]; do
    sleep 0.1
    _i=$((_i + 1))
done
sleep 0.2
kill -INT "$_jbpid" 2>/dev/null
wait "$_jbpid"
_rc=$?
if [ "$_rc" -eq 130 ]; then
    pass "SIGINT while reading stdin exits 130"
else
    fail "SIGINT while reading stdin exits 130" "got $_rc"
fi

_latest=$(newest_session)
if [ -z "$_latest" ]; then
    fail "abort: session dir exists" "no session dir found"
    return 0
fi
pass "abort: session dir exists"

_mstatus=$(jq -r '.status // empty' "$_latest/metadata.json" 2>/dev/null)
if [ "$_mstatus" = "interrupted" ]; then
    pass "abort: metadata status is interrupted"
else
    fail "abort: metadata status is interrupted" "got: $_mstatus"
fi

# ---- Test B (real API): SIGINT mid-stream ----
# A long-generation prompt so the run is certainly still streaming at kill
# time; tolerant if the run somehow finished first (skip, not fail).

echo "Write a 3000-word essay about the history of computing, in full detail." | "$JB" run >/dev/null 2>&1 &
_jbpid=$!
sleep 6
kill -INT "$_jbpid" 2>/dev/null
wait "$_jbpid"
_rc=$?
if [ "$_rc" -ne 130 ]; then
    skip "abort mid-stream" "run exited $_rc before the interrupt landed"
    return 0
fi
pass "SIGINT mid-stream exits 130"

_latest=$(newest_session)
if [ -z "$_latest" ]; then
    fail "abort mid-stream: session dir exists" "no session dir found"
    return 0
fi
_mstatus=$(jq -r '.status // empty' "$_latest/metadata.json" 2>/dev/null)
if [ "$_mstatus" = "interrupted" ]; then
    pass "abort mid-stream: metadata status is interrupted"
else
    fail "abort mid-stream: metadata status is interrupted" "got: $_mstatus"
fi

_sj="$_latest/session.jsonl"
_atail=$(tail -1 "$_sj")
_arole=$(printf '%s' "$_atail" | jq -r '.message.role // empty' 2>/dev/null)
_astop=$(printf '%s' "$_atail" | jq -r '.message.stopReason // empty' 2>/dev/null)
_aerr=$(printf '%s' "$_atail" | jq -r '.message.errorMessage // empty' 2>/dev/null)
if [ "$_arole" = "assistant" ] && [ "$_astop" = "aborted" ]; then
    pass "abort mid-stream: last entry is assistant stopReason aborted"
else
    fail "abort mid-stream: last entry is assistant stopReason aborted" \
        "role=$_arole stop=$_astop"
fi
if [ -n "$_aerr" ]; then
    pass "abort mid-stream: entry carries errorMessage"
else
    fail "abort mid-stream: entry carries errorMessage" "missing"
fi

# The aborted entry chains to the previous entry (the user message)
_prev=$(sed -n '2p' "$_sj")
_prev_id=$(printf '%s' "$_prev" | jq -r '.id // empty' 2>/dev/null)
_apid=$(printf '%s' "$_atail" | jq -r '.parentId' 2>/dev/null)
if [ "$_apid" = "$_prev_id" ]; then
    pass "abort mid-stream: aborted entry chains to the user entry"
else
    fail "abort mid-stream: aborted entry chains to the user entry" \
        "parentId=$_apid prev=$_prev_id"
fi

# Partial text: whatever was streamed before the signal (tolerant — a slow
# first-token time may leave the content block absent)
_atxt=$(printf '%s' "$_atail" | jq -r '.message.content[0].text // empty' 2>/dev/null)
if [ -n "$_atxt" ]; then
    pass "abort mid-stream: partial text recorded"
else
    skip "abort mid-stream: partial text recorded" "no text streamed before the signal"
fi
