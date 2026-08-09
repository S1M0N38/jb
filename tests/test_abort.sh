# test_abort.sh — SIGINT mid-run: aborted entry + status error (reference §6;
# the plan's "metadata interrupted" was superseded — a killed session is a
# kind of failure, and wait/commit/status only understand the reference enum)

repo_init

# ---- Test A (deterministic, no API): SIGINT while stdin is still open ----
# jb blocks reading the prompt; the session exists but nothing was written
# yet. SIGINT must still close the session with status error and exit 130.

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
if [ "$_mstatus" = "error" ]; then
    pass "abort: metadata status is error"
else
    fail "abort: metadata status is error" "got: $_mstatus"
fi

# ---- review fix B: a killed session is terminal (wait exits 1) and
# committable (status error, like any failed run) ----

_uuid=$(basename "$_latest")
_wout=$("$JB" wait "$_uuid" 2>/dev/null)
_wrc=$?
if [ "$_wrc" -eq 1 ]; then
    pass "abort: jb wait on a killed session exits 1 (terminal error)"
else
    fail "abort: jb wait on a killed session exits 1 (terminal error)" "got $_wrc"
fi

_cout=$("$JB" commit "$_uuid" -m "killed mid-run" 2>&1)
_crc=$?
if [ "$_crc" -eq 0 ]; then
    pass "abort: a killed session is committable (-m)"
else
    fail "abort: a killed session is committable (-m)" "got: $_cout"
fi
_cstatus=$(jq -r '.status // empty' "$_latest/metadata.json" 2>/dev/null)
if [ "$_cstatus" = "committed" ]; then
    pass "abort: commit of a killed session lands status committed"
else
    fail "abort: commit of a killed session lands status committed" "got: $_cstatus"
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
if [ "$_mstatus" = "error" ]; then
    pass "abort mid-stream: metadata status is error"
else
    fail "abort mid-stream: metadata status is error" "got: $_mstatus"
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

# The aborted entry chains to the LAST entry before it (the single-branch
# chain) — whether that is the user message (abort during turn 1) or a
# toolResult (abort during a later turn) depends on the model; the
# invariant is parentId == the id of the previous line.
_prev=$(tail -2 "$_sj" | head -1)
_prev_id=$(printf '%s' "$_prev" | jq -r '.id // empty' 2>/dev/null)
_apid=$(printf '%s' "$_atail" | jq -r '.parentId' 2>/dev/null)
if [ "$_apid" = "$_prev_id" ]; then
    pass "abort mid-stream: aborted entry chains to the previous entry"
else
    fail "abort mid-stream: aborted entry chains to the previous entry" \
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

# ---- phase 5: the events stream still closes properly ----
# The interrupted run emits message_end (aborted) + agent_end — the stream
# never dangles.

_ev="$_latest/events.jsonl"
if [ ! -f "$_ev" ]; then
    fail "abort mid-stream: events.jsonl exists" "missing $_ev"
    return 0
fi
pass "abort mid-stream: events.jsonl exists"

_out=$(python3 -c "
import json
lines=[json.loads(l) for l in open('$_ev')]
types=[l['type'] for l in lines]
assert 'agent_start' in types, 'no agent_start'
assert types[-1]=='agent_end', 'last line must be agent_end, got %s' % types[-1]
me=[l for l in lines if l['type']=='message_end']
assert me, 'no message_end'
m=me[-1]['message']
assert m['role']=='assistant' and m['stopReason']=='aborted', 'last message_end not aborted'
assert m.get('errorMessage'), 'no errorMessage on aborted message_end'
si=next(i for i,l in enumerate(lines) if l['type']=='message_start')
ei=next(i for i,l in enumerate(lines) if l['type']=='message_end')
ai=next(i for i,l in enumerate(lines) if l['type']=='agent_end')
assert si<ei<ai, 'order: message_start < message_end < agent_end'
print('OK', len(lines), 'lines')
" 2>&1)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "abort mid-stream: message_end (aborted) + agent_end close the stream ($_out)"
else
    fail "abort mid-stream: message_end (aborted) + agent_end close the stream" "$_out"
fi
