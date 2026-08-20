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

# ---- The pi entry chain (phase 3) ----

# A completed text-only run leaves: header + user + assistant (≥ 3 lines)
_nlines=$(wc -l < "$_sj" | tr -d ' ')
if [ "$_nlines" -ge 3 ]; then
    pass "session.jsonl has header + user + assistant entries"
else
    fail "session.jsonl has header + user + assistant entries" "$_nlines lines"
fi

# Every entry: unique 8-hex id, ISO-ms timestamp
_id_ok=1
_ts_ok=1
while IFS= read -r _line; do
    case "$_line" in
        '{"type":"session"'*) continue ;;   # header
    esac
    _eid=$(printf '%s' "$_line" | jq -r '.id // empty' 2>/dev/null)
    _ets=$(printf '%s' "$_line" | jq -r '.timestamp // empty' 2>/dev/null)
    case "$_eid" in
        [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]) ;;
        *) _id_ok=0 ;;
    esac
    case "$_ets" in
        *T*.*Z) ;;
        *) _ts_ok=0 ;;
    esac
done < "$_sj"
if [ "$_id_ok" -eq 1 ]; then
    pass "every entry id is 8 hex chars"
else
    fail "every entry id is 8 hex chars" "got: $_eid"
fi
if [ "$_ts_ok" -eq 1 ]; then
    pass "every entry timestamp is ISO with milliseconds"
else
    fail "every entry timestamp is ISO with milliseconds" "got: $_ets"
fi

# Entry ids are unique across the session
_dups=$(sed -n '2,$p' "$_sj" | jq -r '.id' 2>/dev/null | sort | uniq -d)
if [ -z "$_dups" ]; then
    pass "entry ids are unique"
else
    fail "entry ids are unique" "duplicates: $_dups"
fi

# The parentId chain: first entry null, then each entry's parentId is the
# previous entry's id (single-branch, linear — pi's branch-export shape)
_chain_ok=1
_prev_id=""
_lineno=0
_chain_err=""
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
    pass "entries form a parentId chain (null first)"
else
    fail "entries form a parentId chain (null first)" "$_chain_err"
fi

# ---- The user entry (line 2) ----

_uent=$(sed -n '2p' "$_sj")
_erole=$(printf '%s' "$_uent" | jq -r '.message.role // empty' 2>/dev/null)
_eblock=$(printf '%s' "$_uent" | jq -r '.message.content[0].type // empty' 2>/dev/null)
_etext=$(printf '%s' "$_uent" | jq -r '.message.content[0].text // empty' 2>/dev/null)
_emts=$(printf '%s' "$_uent" | jq -r '.message.timestamp // empty' 2>/dev/null)

if [ "$_erole" = "user" ]; then
    pass "user entry role is user"
else
    fail "user entry role is user" "got: $_erole"
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
case "$_emts" in
    *[0-9]*) pass "message.timestamp present (epoch ms)" ;;
    *)       fail "message.timestamp present (epoch ms)" "got: $_emts" ;;
esac

# ---- The assistant entry (phase 3: pi writer) — the text block may sit
# at any index when the provider streamed reasoning (thinking owns 0) ----

_atail=$(tail -1 "$_sj")
_arole=$(printf '%s' "$_atail" | jq -r '.message.role // empty' 2>/dev/null)
_ablock=$(printf '%s' "$_atail" | jq -r '[.message.content[]? | select(.type=="text")][0].type // empty' 2>/dev/null)
_atxt=$(printf '%s' "$_atail" | jq -r '[.message.content[]? | select(.type=="text")][0].text // empty' 2>/dev/null)
_aapi=$(printf '%s' "$_atail" | jq -r '.message.api // empty' 2>/dev/null)
_aprov=$(printf '%s' "$_atail" | jq -r '.message.provider // empty' 2>/dev/null)
_amodel=$(printf '%s' "$_atail" | jq -r '.message.model // empty' 2>/dev/null)
_astop=$(printf '%s' "$_atail" | jq -r '.message.stopReason // empty' 2>/dev/null)
_amts=$(printf '%s' "$_atail" | jq -r '.message.timestamp // empty' 2>/dev/null)

if [ "$_arole" = "assistant" ]; then
    pass "last entry role is assistant"
else
    fail "last entry role is assistant" "got: $_arole"
fi
if [ "$_ablock" = "text" ]; then
    pass "assistant content is a text block"
else
    fail "assistant content is a text block" "got: $_ablock"
fi
case "$_atxt" in
    *PONG*) pass "assistant entry holds the answer text" ;;
    *)      fail "assistant entry holds the answer text" "got: $(echo "$_atxt" | head -c 80)" ;;
esac
if [ "$_aapi" = "openai-completions" ]; then
    pass "assistant entry api is openai-completions"
else
    fail "assistant entry api is openai-completions" "got: $_aapi"
fi
if [ -n "$_aprov" ] && [ "$_aprov" != "null" ]; then
    pass "assistant entry provider is the api host"
else
    fail "assistant entry provider is the api host" "got: $_aprov"
fi
if [ "$_astop" = "stop" ]; then
    pass "assistant entry stopReason is stop"
else
    fail "assistant entry stopReason is stop" "got: $_astop"
fi
case "$_amts" in
    *[0-9]*) pass "assistant message.timestamp present (epoch ms)" ;;
    *)       fail "assistant message.timestamp present (epoch ms)" "got: $_amts" ;;
esac

# model matches the effective config (skip when the copied config has none)
_cfgmodel=$(jq -r '.model // empty' "$SCRATCH/.config/jb/config.json" 2>/dev/null)
if [ -n "$_cfgmodel" ]; then
    if [ "$_amodel" = "$_cfgmodel" ]; then
        pass "assistant entry model matches config"
    else
        fail "assistant entry model matches config" "entry=$_amodel config=$_cfgmodel"
    fi
else
    pass "assistant entry model matches config (no config model to compare)"
fi

# usage object: input/output/totalTokens ≥ 0, cost all zeros (jb has no pricing)
if printf '%s' "$_atail" | jq -e '.message.usage | (.input >= 0) and (.output >= 0) and (.totalTokens >= 0)' \
    >/dev/null 2>&1; then
    pass "assistant entry usage has input/output/totalTokens"
else
    fail "assistant entry usage has input/output/totalTokens" \
        "$(printf '%s' "$_atail" | jq -c '.message.usage // "missing"' 2>/dev/null)"
fi
if printf '%s' "$_atail" | jq -e '.message.usage.cost.total == 0' >/dev/null 2>&1; then
    pass "assistant entry usage cost is zeroed"
else
    fail "assistant entry usage cost is zeroed" \
        "$(printf '%s' "$_atail" | jq -c '.message.usage.cost // "missing"' 2>/dev/null)"
fi

# events.jsonl starts with the same v3 header
_ehead=$(head -1 "$_latest/events.jsonl")
if [ "$_ehead" = "$_head" ]; then
    pass "events.jsonl starts with the same header"
else
    fail "events.jsonl starts with the same header" "events: $(echo "$_ehead" | head -c 80)"
fi
