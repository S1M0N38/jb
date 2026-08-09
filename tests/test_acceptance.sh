# test_acceptance.sh — reference §11 verification checklist, end-to-end
# (plan phase 9). One real session, then every §11 step in order: build &
# smoke, structural invariants, export round-trip, events stream shape,
# lifecycle (status → commit → fork → log --graph). The optional pi
# cross-check (pi --session, pi --export) runs only when `command -v pi`
# succeeds. The gist → pi.dev step of the acceptance prose is manual: it
# needs gh auth and uploads outside the scratch dir (rule 4), so it is
# deliberately not automated — see reference §8.

repo_init

# ---- build & smoke (§11) ----

# the binary must build from a clean tree (make test already depends on
# the target, so this is a cheap no-op verification)
if make -C "$REPO_ROOT" >/dev/null 2>&1; then
    pass "make succeeds"
else
    fail "make succeeds" "make failed"
fi

_out=$(prompt_pong | "$JB" run 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "echo PONG | jb run exits 0"
else
    fail "echo PONG | jb run exits 0" "got exit $_rc"
fi

_latest=$(newest_session)
_uuid=$(basename "$_latest" 2>/dev/null)
if [ -z "$_latest" ]; then
    fail "ls .jb/sessions/<uuid>/" "no session dir found"
    return 0
fi

# the three files per reference §2
for _f in metadata.json session.jsonl events.jsonl; do
    if [ -f "$_latest$_f" ]; then
        pass "session dir contains $_f"
    else
        fail "session dir contains $_f" "missing"
    fi
done

# ---- session.jsonl structural invariants (§11, same as a real pi export) ----

if command -v python3 >/dev/null 2>&1; then
    _out=$(python3 -c "
import json,sys
lines=[json.loads(l) for l in open('$_latest/session.jsonl')]
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
else
    skip "structural invariants" "python3 not available"
fi

# ---- events.jsonl is a valid pi-json-mode stream (§11) ----

# first line is the same session header; a completed run has agent_start
# and agent_end
_head=$(head -1 "$_latest/events.jsonl")
_htype=$(printf '%s' "$_head" | jq -r '.type // empty' 2>/dev/null)
_hver=$(printf '%s' "$_head" | jq -r '.version // empty' 2>/dev/null)
if [ "$_htype" = "session" ] && [ "$_hver" = "3" ]; then
    pass "events.jsonl starts with the session header"
else
    fail "events.jsonl starts with the session header" "got: $(echo "$_head" | head -c 120)"
fi

for _ev in agent_start agent_end; do
    if grep -q "\"type\":\"$_ev\"" "$_latest/events.jsonl"; then
        pass "events.jsonl contains $_ev"
    else
        fail "events.jsonl contains $_ev" "missing"
    fi
done

# ---- jb export (§11) ----

_hout="$SCRATCH/jb.html"
_lout="$SCRATCH/jb.jsonl"
_out=$("$JB" export "$_uuid" "$_hout" 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ] && [ -s "$_hout" ]; then
    pass "jb export <uuid> out.html succeeds"
else
    fail "jb export <uuid> out.html succeeds" "rc=$_rc"
fi

_out=$("$JB" export "$_uuid" "$_lout" 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ] && cmp -s "$_latest/session.jsonl" "$_lout"; then
    pass "jb export <uuid> out.jsonl is byte-identical to session.jsonl"
else
    fail "jb export <uuid> out.jsonl is byte-identical to session.jsonl" "rc=$_rc"
fi

# HTML payload round-trip: base64 decodes to {header, entries, leafId,
# systemPrompt, tools} (§11)
if command -v python3 >/dev/null 2>&1; then
    _out=$(python3 -c "
import re,base64,json,sys
src=open('$_hout').read()
m=re.search(r'<script id=\"session-data\"[^>]*>(.*?)</script>',src,re.S)
assert m, 'no session-data script tag'
data=json.loads(base64.b64decode(m.group(1)))
assert set(data)=={'header','entries','leafId','systemPrompt','tools'}, sorted(data)
print('OK', len(data['entries']), 'entries')" 2>&1)
    _rc=$?
    if [ "$_rc" -eq 0 ]; then
        pass "export payload keys round-trip ($_out)"
    else
        fail "export payload keys round-trip" "$_out"
    fi
else
    skip "export payload keys round-trip" "python3 not available"
fi

# ---- lifecycle (§11) ----

# jb status: session listed + repo summary (the id shown is the 8-hex
# short form; without $JB_SESSION in the shell, status shows the
# awaiting-commit line)
_sout=$("$JB" status 2>/dev/null)
case "$_sout" in
    *"${_uuid:0:8}"*) pass "jb status lists the session" ;;
    *) fail "jb status lists the session" "short id ${_uuid:0:8} not in status output" ;;
esac
case "$_sout" in
    *"repo: 1 sessions"*) pass "jb status shows the repo summary" ;;
    *) fail "jb status shows the repo summary" "got: $(echo "$_sout" | head -2)" ;;
esac

# commit (auto message — real LLM, JSON-only); session.jsonl unchanged
_before=$(shasum "$_latest/session.jsonl" | awk '{print $1}')
_cout=$("$JB" commit "$_uuid" 2>&1)
_rc=$?
_after=$(shasum "$_latest/session.jsonl" | awk '{print $1}')
if [ "$_rc" -eq 0 ]; then
    pass "jb commit <uuid> (auto message) exits 0"
else
    fail "jb commit <uuid> (auto message) exits 0" "got: $_cout"
fi
if [ "$_before" = "$_after" ]; then
    pass "commit leaves session.jsonl byte-identical"
else
    fail "commit leaves session.jsonl byte-identical" "file changed"
fi

_cstatus=$(jq -r '.status // empty' "$_latest/metadata.json" 2>/dev/null)
if [ "$_cstatus" = "committed" ]; then
    pass "metadata status is committed after jb commit"
else
    fail "metadata status is committed after jb commit" "got: $_cstatus"
fi

# fork: new session, parentSession in header
_fout=$(echo "continue" | "$JB" run --fork "$_uuid" 2>/dev/null)
_rc=$?
_flatest=$(newest_session)
_fuuid=$(basename "$_flatest" 2>/dev/null)
if [ "$_rc" -eq 0 ] && [ "$_fuuid" != "$_uuid" ]; then
    pass "echo continue | jb run --fork <uuid> creates a new session"
else
    fail "echo continue | jb run --fork <uuid> creates a new session" "rc=$_rc"
fi
_fhead=$(head -1 "$_flatest/session.jsonl")
_fps=$(printf '%s' "$_fhead" | jq -r '.parentSession // empty' 2>/dev/null)
case "$_fps" in
    *"$_uuid"*) pass "fork header carries parentSession" ;;
    *) fail "fork header carries parentSession" "got: $_fps" ;;
esac
_fparent=$(jq -r '.parent // empty' "$_flatest/metadata.json" 2>/dev/null)
if [ "$_fparent" = "$_uuid" ]; then
    pass "fork metadata records parent"
else
    fail "fork metadata records parent" "got: $_fparent"
fi

# commit the fork too (with -m — the auto message was already exercised on
# the root) so the committed forest shows both sessions with the fork edge
_cout=$("$JB" commit "$_fuuid" -m "continue the session" 2>&1)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "jb commit <fork> -m finalizes the fork"
else
    fail "jb commit <fork> -m finalizes the fork" "got: $_cout"
fi

# jb log --graph: both sessions, fork edge (root committed + fork committed)
_gout=$("$JB" log --graph 2>/dev/null)
case "$_gout" in
    *"${_uuid:0:8}"*) pass "jb log --graph shows the root session" ;;
    *) fail "jb log --graph shows the root session" "got: $(echo "$_gout" | head -3)" ;;
esac
case "$_gout" in
    *"${_fuuid:0:8}"*) pass "jb log --graph shows the fork session" ;;
    *) fail "jb log --graph shows the fork session" "got: $(echo "$_gout" | head -3)" ;;
esac
case "$_gout" in
    *"fork"*) pass "jb log --graph shows the fork kind" ;;
    *) fail "jb log --graph shows the fork kind" "got: $(echo "$_gout" | head -3)" ;;
esac

# ---- optional pi cross-check (§11) — guarded by `command -v pi` ----

if command -v pi >/dev/null 2>&1; then
    # work on a copy so pi-side session writes never touch the jb record
    _pcopy="$SCRATCH/pi-check.jsonl"
    cp "$_latest/session.jsonl" "$_pcopy"

    # pi itself accepts the file: reads it as a session and answers. The
    # --model pins pi to the same provider seam as the rest of the suite
    # (reference §2.1: config points at the real provider) — pi's default
    # provider may be quota-limited or unauthed on this machine. --api-key
    # is required because the harness overrides HOME/XDG_CONFIG_HOME, so
    # pi's stored credentials are invisible.
    if pi --session "$_pcopy" -p "reply with exactly the word PONG" \
           --model "opencode-go/deepseek-v4-flash" \
           --api-key "$JB_API_KEY" >/dev/null 2>&1; then
        pass "pi --session reads a jb session file"
    else
        fail "pi --session reads a jb session file" "pi rejected the file"
    fi

    # pi renders it with its own exporter
    _pref="$SCRATCH/pi-ref.html"
    if pi --export "$_pcopy" "$_pref" >/dev/null 2>&1 && [ -s "$_pref" ]; then
        pass "pi --export renders a jb session file"
    else
        fail "pi --export renders a jb session file" "export failed"
    fi
else
    skip "pi cross-check (pi --session / pi --export)" "pi not installed"
fi

# Manual acceptance (not automated — needs gh auth, writes outside the
# scratch): gh gist create --public=false jb-session-<id>.html → renders
# on https://pi.dev/session/#<gistId>
