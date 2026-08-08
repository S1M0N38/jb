# test_state.sh — session.jsonl is the pi-format conversation record (v3)

repo_init
prompt_pong | "$JB" run >/dev/null 2>&1
_latest=$(newest_session)
if [ -z "$_latest" ]; then
    fail "session.jsonl checks" "no session dir found"
    return 0   # sourced by run.sh — return, never exit
fi
_sj="$_latest/session.jsonl"
_uuid=$(basename "$_latest")

# Line 1 is the v3 session header
_head=$(head -1 "$_sj")
_htype=$(printf '%s' "$_head" | jq -r '.type // empty' 2>/dev/null)
_hver=$(printf '%s' "$_head" | jq -r '.version // empty' 2>/dev/null)
if [ "$_htype" = "session" ] && [ "$_hver" = "3" ]; then
    pass "session.jsonl starts with v3 header"
else
    fail "session.jsonl starts with v3 header" "got: $(echo "$_head" | head -c 120)"
fi

# Header id matches the session dir name
_hid=$(printf '%s' "$_head" | jq -r '.id // empty' 2>/dev/null)
if [ "$_hid" = "$_uuid" ]; then
    pass "header id matches session uuid"
else
    fail "header id matches session uuid" "header=$_hid dir=$_uuid"
fi

# Header timestamp is ISO-ms (e.g. 2026-08-06T00:33:50.332Z)
_hts=$(printf '%s' "$_head" | jq -r '.timestamp // empty' 2>/dev/null)
case "$_hts" in
    *T*.*Z) pass "header timestamp is ISO with milliseconds" ;;
    *)      fail "header timestamp is ISO with milliseconds" "got: $_hts" ;;
esac

# Header carries cwd
_hcwd=$(printf '%s' "$_head" | jq -r '.cwd // empty' 2>/dev/null)
if [ -n "$_hcwd" ]; then
    pass "header carries cwd"
else
    fail "header carries cwd" "missing"
fi

# Exactly 2 lines: header + user entry (assistant entries land in phase 3)
_nlines=$(wc -l < "$_sj" | tr -d ' ')
if [ "$_nlines" -eq 2 ]; then
    pass "session.jsonl has header + user entry"
else
    fail "session.jsonl has header + user entry" "$_nlines lines"
fi

# The user entry has the pi base shape (§3.2/3.3)
_entry=$(tail -1 "$_sj")
_etype=$(printf '%s' "$_entry" | jq -r '.type // empty' 2>/dev/null)
_eid=$(printf '%s' "$_entry" | jq -r '.id // empty' 2>/dev/null)
_epid=$(printf '%s' "$_entry" | jq -r '.parentId' 2>/dev/null)
_erole=$(printf '%s' "$_entry" | jq -r '.message.role // empty' 2>/dev/null)
_eblock=$(printf '%s' "$_entry" | jq -r '.message.content[0].type // empty' 2>/dev/null)
_etext=$(printf '%s' "$_entry" | jq -r '.message.content[0].text // empty' 2>/dev/null)
_emts=$(printf '%s' "$_entry" | jq -r '.message.timestamp // empty' 2>/dev/null)

if [ "$_etype" = "message" ]; then
    pass "user entry type is message"
else
    fail "user entry type is message" "got: $_etype"
fi
case "$_eid" in
    [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f])
        pass "entry id is 8 hex chars" ;;
    *) fail "entry id is 8 hex chars" "got: $_eid" ;;
esac
if [ "$_epid" = "null" ]; then
    pass "first entry parentId is null"
else
    fail "first entry parentId is null" "got: $_epid"
fi
if [ "$_erole" = "user" ]; then
    pass "entry role is user"
else
    fail "entry role is user" "got: $_erole"
fi
if [ "$_eblock" = "text" ]; then
    pass "user content is a text block"
else
    fail "user content is a text block" "got: $_eblock"
fi
case "$_etext" in
    *PONG*) pass "user entry holds the prompt text" ;;
    *)      fail "user entry holds the prompt text" "got: $(echo "$_etext" | head -c 80)" ;;
esac
if [ -n "$_emts" ] && [ "$_emts" != "null" ]; then
    pass "message.timestamp present (epoch ms)"
else
    fail "message.timestamp present (epoch ms)" "got: $_emts"
fi

# events.jsonl starts with the same v3 header
_ehead=$(head -1 "$_latest/events.jsonl")
if [ "$_ehead" = "$_head" ]; then
    pass "events.jsonl starts with the same header"
else
    fail "events.jsonl starts with the same header" "events: $(echo "$_ehead" | head -c 80)"
fi
