# test_export.sh — jb export ID [PATH] (plan phase 8)
# The pi viewer: .jsonl = byte-parity copy of session.jsonl; .html =
# self-contained viewer (embedded vendored pi-export template, payload
# round-trip); ID rules (full uuid / unique 4+ hex prefix / @); snapshot
# semantics for working sessions. Fixture repos only — export is offline.

repo_init

_a="21637177a1b2c3d4e5f60718293a4b5c6"
_b="21637178c1d2e3f405162738495a6b7c8"

# fixture_entries — append a hand-built pi-format conversation (user →
# assistant) to a fixture session's session.jsonl. The ids/timestamps are
# fixed literals so assertions stay exact.
fixture_entries() {
    _fe_uuid="$1"
    cat >> "$JB_SESSIONS_DIR/$_fe_uuid/session.jsonl" <<'EOF'
{"type":"message","id":"a1b2c3d4","parentId":null,"timestamp":"2026-08-06T00:33:50.332Z","message":{"role":"user","content":[{"type":"text","text":"hello"}],"timestamp":1789072430332}}
{"type":"message","id":"e5f60718","parentId":"a1b2c3d4","timestamp":"2026-08-06T00:33:52.100Z","message":{"role":"assistant","content":[{"type":"text","text":"PONG"}],"api":"openai-completions","provider":"openai","model":"gpt-4.1","usage":{"input":10,"output":2,"totalTokens":12},"stopReason":"stop","timestamp":1789072432100}}
EOF
}

fixture_session "$_a" completed "export me"
fixture_entries "$_a"

# ---- usage & gating ----

assert_exit 2 "jb export without an ID exits 2" "$JB" export
assert_exit 2 "jb export with too many args exits 2" "$JB" export "$_a" a.jsonl b.jsonl

# ---- .jsonl: byte-parity copy (pi exportToJsonl shape) ----

_out=$("$JB" export "$_a" out.jsonl)
_rc=$?
if [ "$_rc" -eq 0 ] && cmp -s "$JB_SESSIONS_DIR/$_a/session.jsonl" out.jsonl; then
    pass "jb export <id> out.jsonl is byte-identical to session.jsonl"
else
    fail "jb export <id> out.jsonl is byte-identical to session.jsonl" "rc=$_rc"
fi

case "$_out" in
    *"exported 21637177 → out.jsonl"*) pass "jb export prints 'exported <id> → <path>'" ;;
    *) fail "jb export prints 'exported <id> → <path>'" "got: $_out" ;;
esac

# importable shape: v3 header + linear chain, entry-for-entry equality
if command -v python3 >/dev/null 2>&1; then
    _imp=$(python3 -c "
import json,sys
a=[json.loads(l) for l in open('$JB_SESSIONS_DIR/$_a/session.jsonl')]
b=[json.loads(l) for l in open('out.jsonl')]
assert b==a, 'entries differ'
assert b[0]['type']=='session' and b[0]['version']==3, 'bad header'
ids=[e['id'] for e in b[1:]]
assert len(ids)==len(set(ids)), 'duplicate ids'
for e in b[1:]: assert e['parentId'] in (None,*ids), 'broken chain'
print('OK', len(b)-1, 'entries')" 2>&1)
    _rc=$?
    if [ "$_rc" -eq 0 ]; then
        pass "jb export .jsonl is importable (v3 header + linear chain) ($_imp)"
    else
        fail "jb export .jsonl is importable (v3 header + linear chain)" "$_imp"
    fi
else
    skip "jb export .jsonl is importable (v3 header + linear chain)" "python3 not available"
fi

# ---- ID rules: full uuid / unique 4+ hex prefix / @ ----
_out=$("$JB" export 2163 pre-out.jsonl 2>&1); _rc=$?  # prefix, PATH in scratch
if [ "$_rc" -eq 0 ] && cmp -s "$JB_SESSIONS_DIR/$_a/session.jsonl" pre-out.jsonl; then
    pass "jb export <unique prefix> resolves"
else
    fail "jb export <unique prefix> resolves" "rc=$_rc: $_out"
fi
rm -f pre-out.jsonl

_out=$(JB_SESSION="$_a" "$JB" export @ out2.jsonl 2>&1); _rc=$?
if [ "$_rc" -eq 0 ] && cmp -s "$JB_SESSIONS_DIR/$_a/session.jsonl" out2.jsonl; then
    pass "jb export @ resolves via JB_SESSION"
else
    fail "jb export @ resolves via JB_SESSION" "rc=$_rc: $_out"
fi

_err=$(env -u JB_SESSION "$JB" export @ out.jsonl 2>&1 >/dev/null)
assert_exit 1 "jb export @ without JB_SESSION exits 1" env -u JB_SESSION "$JB" export @ out.jsonl
case "$_err" in
    *"JB_SESSION not set"*) pass "jb export @ without JB_SESSION reports it" ;;
    *) fail "jb export @ without JB_SESSION reports it" "stderr: $_err" ;;
esac

_err=$("$JB" export xyz out.jsonl 2>&1 >/dev/null)
assert_exit 1 "jb export <unknown id> exits 1" "$JB" export xyz out.jsonl
case "$_err" in
    *"no session 'xyz'"*) pass "jb export <unknown id> reports no session" ;;
    *) fail "jb export <unknown id> reports no session" "stderr: $_err" ;;
esac

# ambiguous: two sessions sharing a 6-hex prefix
fixture_session "$_b" completed "second"
fixture_entries "$_b"
_err=$("$JB" export 216371 out.jsonl 2>&1 >/dev/null)
assert_exit 1 "jb export <ambiguous id> exits 1" "$JB" export 216371 out.jsonl
case "$_err" in
    *"ambiguous id '216371'"*) pass "jb export <ambiguous id> reports it" ;;
    *) fail "jb export <ambiguous id> reports it" "stderr: $_err" ;;
esac

# ---- .html: default path + payload round-trip (reference §8.2/§11) ----

_out=$("$JB" export "$_a")
_rc=$?
if [ "$_rc" -eq 0 ] && [ -f "jb-session-$_a.html" ]; then
    pass "jb export <id> without PATH writes jb-session-<uuid>.html in cwd"
else
    fail "jb export <id> without PATH writes jb-session-<uuid>.html in cwd" "rc=$_rc"
fi
case "$_out" in
    *"→ jb-session-$_a.html"*) pass "jb export prints the default path" ;;
    *) fail "jb export prints the default path" "got: $_out" ;;
esac

# payload round-trip: base64 decodes to {header, entries, leafId, systemPrompt, tools}
if command -v python3 >/dev/null 2>&1; then
    _pay=$(python3 -c "
import re,base64,json
src=open('jb-session-$_a.html').read()
m=re.search(r'<script id=\"session-data\"[^>]*>(.*?)</script>', src, re.S)
assert m, 'no session-data script'
data=json.loads(base64.b64decode(m.group(1)))
assert set(data)=={'header','entries','leafId','systemPrompt','tools'}, sorted(data)
assert data['header']['id']=='$_a', 'header id'
assert len(data['entries'])==2, 'entry count'
assert data['leafId']=='e5f60718', 'leafId'
assert isinstance(data['systemPrompt'], str) and data['systemPrompt'], 'systemPrompt'
assert isinstance(data['tools'], list) and len(data['tools'])==5, 'tools'
print('OK', len(data['entries']), 'entries')" 2>&1)
    _rc=$?
    if [ "$_rc" -eq 0 ]; then
        pass "jb export .html payload round-trip ($_pay)"
    else
        fail "jb export .html payload round-trip" "$_pay"
    fi
else
    skip "jb export .html payload round-trip" "python3 not available"
fi

# the viewer shell: theme vars, vendored highlight.js, session-data script
_grep_ok=1
for _m in '--exportPageBg: #18181e' '--userMessageBg: #343541' 'hljs' 'session-data'; do
    grep -qF -- "$_m" "jb-session-$_a.html" || { _grep_ok=0; _miss="$_m"; }
done
if [ "$_grep_ok" -eq 1 ]; then
    pass "jb export .html embeds theme vars + vendored viewer assets"
else
    fail "jb export .html embeds theme vars + vendored viewer assets" "missing: $_miss"
fi

# non-.jsonl PATH is treated as HTML
if "$JB" export "$_a" out.htm >/dev/null 2>&1 && grep -q "session-data" out.htm; then
    pass "jb export <non-jsonl PATH> produces HTML"
else
    fail "jb export <non-jsonl PATH> produces HTML"
fi

# a fresh session (header only, no entries) still exports
_c="fa39c2a1c1d2e3f405162738495a6b7d8"
fixture_session "$_c" completed "empty"
_out=$("$JB" export "$_c" empty.html 2>&1); _rc=$?
if command -v python3 >/dev/null 2>&1; then
    _imp=$(python3 -c "
import re,base64,json
src=open('empty.html').read()
m=re.search(r'<script id=\"session-data\"[^>]*>(.*?)</script>', src, re.S)
data=json.loads(base64.b64decode(m.group(1)))
assert data['entries']==[] and data['leafId'] is None, (data['entries'], data['leafId'])
print('OK')" 2>&1)
    _rc2=$?
    if [ "$_rc" -eq 0 ] && [ "$_rc2" -eq 0 ] && [ "$_imp" = "OK" ]; then
        pass "jb export of a fresh session has empty entries, null leafId"
    else
        fail "jb export of a fresh session has empty entries, null leafId" "rc=$_rc: $_imp"
    fi
else
    if [ "$_rc" -eq 0 ]; then
        skip "jb export of a fresh session has empty entries, null leafId" "python3 not available"
    else
        fail "jb export of a fresh session has empty entries, null leafId" "rc=$_rc"
    fi
fi

# ---- snapshot semantics: working sessions export, no lock ----

_w="fa39c2a1b1c2d3e4f5061728394a5b6d7"
fixture_session "$_w" working "in flight"
fixture_entries "$_w"
_out=$("$JB" export "$_w" work.html 2>&1); _rc=$?
if [ "$_rc" -eq 0 ] && grep -q "session-data" work.html; then
    pass "jb export of a working session succeeds (snapshot, no lock)"
else
    fail "jb export of a working session succeeds (snapshot, no lock)" "rc=$_rc: $_out"
fi
"$JB" export "$_w" work2.html >/dev/null 2>&1
if cmp -s work.html work2.html; then
    pass "jb export is read-only: two exports are identical"
else
    fail "jb export is read-only: two exports are identical"
fi

# ---- write failure ----

assert_exit 1 "jb export to an unwritable path exits 1" "$JB" export "$_a" no-such-dir/out.jsonl

# ---- repo gating ----

new_scratch   # no repo here — repo_init ran in the previous scratch
assert_exit 1 "jb export outside a repo exits 1" "$JB" export "$_a" out.jsonl
