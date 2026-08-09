# test_invariants.sh — structural invariants of a real session (reference §11)
# Independent check via python3 — the same assertions the pi team uses on
# real pi exports. Skipped when python3 is unavailable.

command -v python3 >/dev/null 2>&1 || {
    skip "structural invariants" "python3 not available"
    return 0
}

repo_init
prompt_pong | "$JB" run >/dev/null 2>&1
_latest=$(newest_session)
if [ -z "$_latest" ]; then
    fail "structural invariants" "no session dir found"
    return 0
fi
_sj="$_latest/session.jsonl"

# The reference §11 check: v3 header, unique ids, parentId ∈ {null} ∪ ids
_out=$(python3 -c "
import json,sys
lines=[json.loads(l) for l in open('$_sj')]
assert lines[0]['type']=='session' and lines[0]['version']==3, 'bad header'
ids=[e['id'] for e in lines[1:]]
assert len(ids)==len(set(ids)), 'duplicate ids'
for e in lines[1:]: assert e['parentId'] in (None,*ids), 'broken chain'
print('OK', len(lines)-1, 'entries')" 2>&1)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "invariants: v3 header, unique ids, parentId chain ($_out)"
else
    fail "invariants: v3 header, unique ids, parentId chain" "$_out"
fi

# Role sequence: user → assistant → (toolResult)* — no other roles, no gaps
_seq=$(python3 -c "
import json,sys
lines=[json.loads(l) for l in open('$_sj')]
roles=[e['message']['role'] for e in lines[1:]]
assert roles[0]=='user', 'must start with user'
for i,r in enumerate(roles):
    if r=='assistant':
        assert i==len(roles)-1 or roles[i+1] in ('assistant','toolResult'), 'assistant must be followed by assistant/toolResult'
    elif r=='toolResult':
        assert roles[i-1]=='assistant', 'toolResult must follow assistant'
    else:
        assert r=='user' and i==0, 'unexpected role'
print('OK', ' -> '.join(roles))" 2>&1)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "invariants: role sequence user → assistant → (toolResult)* ($_seq)"
else
    fail "invariants: role sequence user → assistant → (toolResult)*" "$_seq"
fi

# Stop reasons are from the closed set
_stops=$(python3 -c "
import json,sys
lines=[json.loads(l) for l in open('$_sj')]
stops={e['message'].get('stopReason') for e in lines[1:] if e['message']['role']=='assistant'}
assert stops <= {'stop','toolUse','error','aborted'}, stops
print('OK', sorted(stops))" 2>&1)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "invariants: stopReason from closed set ($_stops)"
else
    fail "invariants: stopReason from closed set" "$_stops"
fi

# ---- review fix C: real milliseconds ----
# The seconds-mod-1000 bug is indistinguishable from real ms by value alone
# (always within ±1s of the wall clock), so the reliable detector is
# COHERENCE: the entry's ISO-ms timestamp and its message.timestamp
# (epoch ms) are generated back-to-back from the same clock, so they must
# agree to <100ms. The buggy version (ISO .mmm = seconds%1000, epoch =
# seconds*1000) diverges by seconds%1000 — up to ~1s, and >100ms whenever
# seconds%1000 > 100. Also sanity-check the header parses and is recent.
_ms=$(python3 -c "
import json,sys,time,datetime
UTC=datetime.timezone.utc
lines=[json.loads(l) for l in open('$_sj')]
h=lines[0]['timestamp']
t=datetime.datetime.strptime(h, '%Y-%m-%dT%H:%M:%S.%fZ').replace(tzinfo=UTC)
iso_ms=t.timestamp()*1000
now_ms=time.time()*1000
assert 0 <= abs(iso_ms-now_ms) < 300000, ('header timestamp not recent: %s' % h)
e=lines[1]
et=datetime.datetime.strptime(e['timestamp'], '%Y-%m-%dT%H:%M:%S.%fZ').replace(tzinfo=UTC)
ems=e['message'].get('timestamp')
assert ems, 'missing message.timestamp'
assert abs(et.timestamp()*1000-ems) < 100, 'entry ISO-ms and message epoch-ms disagree (%.3f vs %d) — fake ms' % (et.timestamp()*1000, ems)
# message.timestamp must also be a genuine epoch-ms within a minute
assert abs(ems-now_ms) < 60000, 'message.timestamp not epoch-ms: %s' % ems
print('OK iso=%.3f epoch=%d skew=%.1fms' % (et.timestamp()*1000, ems, abs(et.timestamp()*1000-ems)))" 2>&1)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "invariants: timestamps carry real milliseconds ($_ms)"
else
    fail "invariants: timestamps carry real milliseconds" "$_ms"
fi
