#!/bin/sh
# test_events.sh — events.jsonl, the live stream (phase 5)
# pi json-mode wire protocol (reference §5): session header, agent_start …
# agent_end, delta-only message events, tool execution events. Structural
# checks via python3 (skipped when unavailable), like test_invariants.sh.
# Determinism from invariant assertions (plan §2.2), never exact values.

command -v python3 >/dev/null 2>&1 || {
    skip "events stream" "python3 not available"
    return 0
}

repo_init
prompt_pong | "$JB" run >/dev/null 2>&1
_latest=$(newest_session)
if [ -z "$_latest" ]; then
    fail "events: session dir exists" "no session dir found"
    return 0
fi
_ev="$_latest/events.jsonl"
_sj="$_latest/session.jsonl"
if [ ! -f "$_ev" ]; then
    fail "events: events.jsonl exists" "missing $_ev"
    return 0
fi
pass "events: events.jsonl exists"

# ---- slice 1: stream shape ----
# First line is the session header (same object as session.jsonl's); an
# agent_start is present; agent_end is the last line and carries the
# messages array; every line is JSON of a known type (the 8 line types).

_out=$(python3 -c "
import json,sys
ev=open('$_ev').read().splitlines()
sj=open('$_sj').read().splitlines()
assert ev and sj, 'empty files'
assert ev[0]==sj[0], 'events header != session.jsonl header'
hdr=json.loads(ev[0])
assert hdr['type']=='session' and hdr['version']==3, 'bad header'
lines=[json.loads(l) for l in ev]
types=[l['type'] for l in lines]
assert types[0]=='session', 'first line must be the session header'
assert 'agent_start' in types, 'no agent_start'
assert types[-1]=='agent_end', 'last line must be agent_end'
known={'session','agent_start','message_start','message_update',
       'message_end','tool_execution_start','tool_execution_end','agent_end'}
assert set(types)<=known, 'unknown line type: %s' % (set(types)-known)
assert len(lines[-1].get('messages',[]))>=1, 'agent_end messages empty'
print('OK', len(lines), 'lines')
" 2>&1)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "events: stream shape — header, agent_start, agent_end last ($_out)"
else
    fail "events: stream shape — header, agent_start, agent_end last" "$_out"
fi

# ---- slice 2: message events ----
# message_start (role assistant, stopReason pending) … message_end (final
# message with usage); updates are delta-only — no cumulative message, no
# partial — and contentIndex is monotonic non-decreasing.

_out=$(python3 -c "
import json
lines=[json.loads(l) for l in open('$_ev')]
ms=[l for l in lines if l['type']=='message_start']
me=[l for l in lines if l['type']=='message_end']
assert ms, 'no message_start'
for l in ms:
    m=l['message']
    assert m['role']=='assistant', 'message_start role'
    assert m['stopReason']=='pending', 'message_start stopReason'
    assert m['content']==[], 'message_start content'
assert me, 'no message_end'
for l in me:
    m=l['message']
    assert m['role']=='assistant', 'message_end role'
    assert m['stopReason'] in ('stop','toolUse','error','aborted'), m['stopReason']
    assert 'api' in m and 'provider' in m and 'model' in m, 'message_end api/provider/model'
    if 'usage' in m:
        assert set(m['usage'])>= {'input','output','totalTokens'}, 'usage keys'
assert len(ms)==len(me), 'message_start/end not paired'
# delta-only updates: no cumulative message, no partial
ups=[l for l in lines if l['type']=='message_update']
assert ups, 'no message_update'
for l in ups:
    assert set(l.keys())=={'type','assistantMessageEvent'}, 'update carries more than assistantMessageEvent'
    ame=l['assistantMessageEvent']
    assert 'partial' not in ame and 'cumulative' not in ame, 'update not delta-only'
    assert ame['type'] in ('text_delta','thinking_delta','toolcall_start','toolcall_delta','toolcall_end'), ame['type']
    assert isinstance(ame['contentIndex'], int), 'contentIndex missing'
# contentIndex monotonic non-decreasing across the stream
idxs=[l['assistantMessageEvent']['contentIndex'] for l in ups]
assert all(b>=a for a,b in zip(idxs,idxs[1:])), 'contentIndex not monotonic: %s' % idxs
print('OK', len(ms), 'message(s),', len(ups), 'updates')
" 2>&1)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "events: message_start/update/end, delta-only, monotonic ($_out)"
else
    fail "events: message_start/update/end, delta-only, monotonic" "$_out"
fi

# ---- slice 3: tool events ----
# A tool-requiring prompt (read a file whose content is not in the system
# prompt). Tolerant per plan §2.2: if the model replies in plain text, the
# tool assertions are skipped. When tools run, tool_execution_start/end
# must carry toolCallId/name/args/result and the events stream must agree
# with the persisted toolCall blocks.

cp "$REPO_ROOT/CONTEXT.md" .
echo "zebra-42-quantum" > secret.txt
echo "Read the file secret.txt in the current directory and reply with exactly its contents, nothing else." | "$JB" run >/dev/null 2>&1
_tool_latest=$(newest_session)
if [ -z "$_tool_latest" ]; then
    fail "events: tool session dir exists" "no session dir found"
    return 0
fi
_tool_sj="$_tool_latest/session.jsonl"
_tool_ev="$_tool_latest/events.jsonl"

_n_tc=$(sed -n '2,$p' "$_tool_sj" | jq -c 'select(.message.role == "assistant") | .message.content[]? | select(.type == "toolCall")' 2>/dev/null | wc -l | tr -d ' ')
if [ "$_n_tc" -eq 0 ]; then
    skip "events: tool_execution events" "model replied without tool calls"
else
    _out=$(python3 -c "
import json
lines=[json.loads(l) for l in open('$_tool_ev')]
sj=[json.loads(l) for l in open('$_tool_sj')]
# persisted toolCall blocks (id → name/arguments)
persisted={}
for e in sj[1:]:
    for b in e['message'].get('content',[]):
        if b.get('type')=='toolCall':
            persisted[b['id']]=(b['name'],b['arguments'])
assert persisted, 'no persisted toolCall blocks'
starts=[l for l in lines if l['type']=='tool_execution_start']
ends=[l for l in lines if l['type']=='tool_execution_end']
by_id={l['toolCallId']:l for l in starts}
by_end={l['toolCallId']:l for l in ends}
assert set(persisted)==set(by_id)==set(by_end), 'tool event ids != persisted ids'
for cid,(name,args) in persisted.items():
    s=by_id[cid]
    assert s['toolName']==name, 'start toolName'
    assert s['args']==args, 'start args != persisted arguments'
    e=by_end[cid]
    assert e['toolName']==name, 'end toolName'
    assert isinstance(e['isError'],bool), 'isError not bool'
    assert e['result']['content'][0]['type']=='text' and e['result']['content'][0]['text'], 'result text empty'
    si=next(i for i,l in enumerate(lines) if l is s)
    ei=next(i for i,l in enumerate(lines) if l is e)
    assert si<ei, 'start after end'
# toolcall_end message updates agree with the persisted blocks
for l in lines:
    ame=l.get('assistantMessageEvent') or {}
    if ame.get('type')=='toolcall_end':
        tc=ame['toolCall']
        assert tc['id'] in persisted and tc['name']==persisted[tc['id']][0], 'toolcall_end mismatch'
        assert tc['arguments']==persisted[tc['id']][1], 'toolcall_end arguments'
print('OK', len(persisted), 'tool call(s)')
" 2>&1)
    _rc=$?
    if [ "$_rc" -eq 0 ]; then
        pass "events: tool_execution_start/end carry id/name/args/result ($_out)"
    else
        fail "events: tool_execution_start/end carry id/name/args/result" "$_out"
    fi
fi
