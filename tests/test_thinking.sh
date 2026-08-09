# test_thinking.sh — reasoning stream (review fix D, Q5-ii).
# The configured provider streams delta.reasoning_content. Assertions are
# CONDITIONAL (plan §2.2): if the assistant entry carries a thinking block,
# its shape must be pi's exactly and the events feed must carry
# thinking_delta; a plain-text reply stays tolerated. Plus a fixture-based
# check that the loader/export handle thinking blocks (fork + export).

repo_init

# ---- real run: thinking block shape + thinking_delta events ----

prompt_pong | "$JB" run >/dev/null 2>&1
_latest=$(newest_session)
if [ -z "$_latest" ]; then
    fail "thinking: no session dir found"
    return 0
fi
_sj="$_latest/session.jsonl"

_out=$(python3 -c "
import json,sys
lines=[json.loads(l) for l in open('$_sj')]
entries=[e['message'] for e in lines[1:]]
think=[m for m in entries if m['role']=='assistant' and any(b.get('type')=='thinking' for b in m.get('content',[]))]
if not think:
    print('NO_THINKING')
    sys.exit(0)
for m in think:
    blocks=m['content']
    tb=[b for b in blocks if b.get('type')=='thinking']
    assert tb, 'thinking block missing'
    for b in tb:
        assert isinstance(b.get('thinking'), str) and b['thinking'], 'thinking text empty'
        assert b.get('thinkingSignature')=='reasoning_content', 'bad thinkingSignature'
        assert blocks.index(b)==0, 'thinking block must precede text'
print('OK', len(think), 'entries with thinking')
" 2>&1)
_rc=$?
case "$_out" in
    NO_THINKING)
        skip "thinking: block shape" "provider sent no reasoning on this run"
        ;;
    OK*)
        pass "thinking: pi block shape, before text, signature set ($_out)"
        ;;
    *)
        fail "thinking: pi block shape, before text, signature set" "$_out"
        ;;
esac

# events: thinking_delta (contentIndex 0) whenever the transcript has a
# thinking block; message_end carries the block too
_ev="$_latest/events.jsonl"
_out=$(python3 -c "
import json,sys
sj=[json.loads(l) for l in open('$_sj')]
ev=[json.loads(l) for l in open('$_ev')]
has_think=any(b.get('type')=='thinking' for e in sj[1:] for b in e['message'].get('content',[]))
if not has_think:
    print('NO_THINKING')
    sys.exit(0)
td=[l for l in ev if l.get('type')=='message_update' and l.get('assistantMessageEvent',{}).get('type')=='thinking_delta']
assert td, 'no thinking_delta events'
for l in td:
    assert l['assistantMessageEvent']['contentIndex']==0, 'thinking_delta contentIndex != 0'
    assert 'delta' in l['assistantMessageEvent'], 'thinking_delta missing delta'
me=[l for l in ev if l.get('type')=='message_end']
assert me, 'no message_end'
last=me[-1]['message']
blocks=last.get('content',[])
tb=[b for b in blocks if b.get('type')=='thinking']
assert tb and tb[0].get('thinkingSignature')=='reasoning_content', 'message_end lacks the thinking block'
print('OK', len(td), 'thinking_delta lines')
" 2>&1)
_rc=$?
case "$_out" in
    NO_THINKING)
        skip "thinking: events stream" "provider sent no reasoning on this run"
        ;;
    OK*)
        pass "thinking: thinking_delta events + message_end carries the block ($_out)"
        ;;
    *)
        fail "thinking: thinking_delta events + message_end carries the block" "$_out"
        ;;
esac

# ---- fixture: loader + wire conversion tolerate thinking blocks ----
# A parent session whose assistant entry carries a thinking block forks
# cleanly (the wire request drops thinking — pi_joined_text only joins
# text blocks — and the fork's transcript keeps the full record).

_thk="cccccccc-1111-4aaa-8bbb-222222222222"
mkdir -p "$JB_SESSIONS_DIR/$_thk"
cat > "$JB_SESSIONS_DIR/$_thk/session.jsonl" <<'EOF'
{"type":"session","version":3,"id":"cccccccc-1111-4aaa-8bbb-222222222222","timestamp":"2026-08-06T00:00:00.000Z","cwd":"/tmp"}
{"type":"message","id":"20000001","parentId":null,"timestamp":"2026-08-06T00:00:00.001Z","message":{"role":"user","content":[{"type":"text","text":"thinking fixture prompt"}],"timestamp":1}}
{"type":"message","id":"20000002","parentId":"20000001","timestamp":"2026-08-06T00:00:00.002Z","message":{"role":"assistant","content":[{"type":"thinking","thinking":"I should reply PONG.","thinkingSignature":"reasoning_content"},{"type":"text","text":"PONG"}],"api":"openai-completions","provider":"local","model":"test-model","usage":{"input":5,"output":2,"cacheRead":0,"cacheWrite":0,"reasoning":3,"totalTokens":7,"cost":{"input":0,"output":0,"cacheRead":0,"cacheWrite":0,"total":0}},"stopReason":"stop","timestamp":2}}
EOF

echo "reply with exactly the word PONG" | "$JB" run --fork "$_thk" >/dev/null 2>"$SCRATCH/thk.err"
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "thinking: fork of a thinking-block session runs (wire drops thinking)"
else
    fail "thinking: fork of a thinking-block session runs (wire drops thinking)" \
        "exit $_rc: $(cat "$SCRATCH/thk.err")"
fi

# the fork's transcript keeps the parent's record (thinking included)
_fork=$(newest_session)
if grep -q 'thinking fixture prompt' "$_fork/session.jsonl" && \
   grep -q 'I should reply PONG' "$_fork/session.jsonl"; then
    pass "thinking: fork transcript keeps the parent's thinking block"
else
    fail "thinking: fork transcript keeps the parent's thinking block" "missing"
fi

# ---- export round-trip with a thinking block ----
_out=$("$JB" export "$_thk" "$SCRATCH/thk.html" 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ] && [ -s "$SCRATCH/thk.html" ]; then
    pass "thinking: export of a thinking-block session succeeds"
else
    fail "thinking: export of a thinking-block session succeeds" "rc=$_rc"
fi
_out=$(python3 -c "
import re,base64,json,sys
src=open('$SCRATCH/thk.html').read()
m=re.search(r'<script id=\"session-data\"[^>]*>(.*?)</script>',src,re.S)
assert m, 'no session-data script tag'
data=json.loads(base64.b64decode(m.group(1)))
assert set(data)=={'header','entries','leafId','systemPrompt','tools'}, sorted(data)
blks=[b for e in data['entries'] for b in e['message'].get('content',[])]
tb=[b for b in blks if b.get('type')=='thinking']
assert tb and tb[0]['thinking']=='I should reply PONG.', 'thinking block lost in export'
print('OK', len(data['entries']), 'entries')
" 2>&1)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "thinking: export payload keeps the thinking block ($_out)"
else
    fail "thinking: export payload keeps the thinking block" "$_out"
fi
