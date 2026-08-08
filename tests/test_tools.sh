# test_tools.sh — agentic loop with tool calls

repo_init

# The scratch cwd is empty; copy a known repo file in so ls/read have a
# stable target that the assertions can rely on.
cp "$REPO_ROOT/CONTEXT.md" .

# Ask jb to list files (should use bash tool)
_out=$(echo "run ls on the current directory and tell me what files you see. Reply with just the filenames, one per line." | "$JB" run 2>/dev/null)

# Check that the response mentions CONTEXT.md (a file we know exists)
case "$_out" in
    *CONTEXT.md*) pass "jb uses bash tool to list files" ;;
    *)            fail "jb uses bash tool to list files" "got: $(echo "$_out" | head -c 300)" ;;
esac

# Ask jb to read a file whose content is NOT in the system prompt, so the
# read tool is effectively required (plan §2.2 — tool scenarios encouraged)
echo "zebra-42-quantum" > secret.txt
_out=$(echo "Read the file secret.txt in the current directory and reply with exactly its contents, nothing else." | "$JB" run 2>/dev/null)

case "$_out" in
    *zebra-42-quantum*) pass "jb uses read tool to read file" ;;
    *) fail "jb uses read tool to read file" "got: $(echo "$_out" | head -c 300)" ;;
esac

# ---- phase 3: the tool-call conversation is recorded in pi format ----

# The read scenario is the newest session; inspect its session.jsonl
_latest=$(newest_session)
if [ -z "$_latest" ]; then
    fail "tool entries in session.jsonl" "no session dir found"
    return 0
fi
_sj="$_latest/session.jsonl"

# Tolerant: if the model replied in plain text without tools, there are no
# toolCall blocks to assert — note and continue (plan §2.2).
_n_tc=$(sed -n '2,$p' "$_sj" | jq -c 'select(.message.role == "assistant") | .message.content[]? | select(.type == "toolCall")' 2>/dev/null | wc -l | tr -d ' ')
if [ -z "$_n_tc" ] || [ "$_n_tc" -eq 0 ]; then
    skip "tool entries in session.jsonl" "model replied without tool calls — nothing to assert"
    return 0
fi

# Tool-call assistant entry: toolCall block with id, name, and a PARSED
# arguments OBJECT (not a string) — reference §3.3/3.4
_tc_entry=$(sed -n '2,$p' "$_sj" | jq -c 'select(.message.role == "assistant") | select((.message.content // []) | map(.type == "toolCall") | any)' 2>/dev/null | head -1)
_tcid=$(printf '%s' "$_tc_entry" | jq -r '.message.content[] | select(.type == "toolCall") | .id // empty' 2>/dev/null | head -1)
_tcname=$(printf '%s' "$_tc_entry" | jq -r '.message.content[] | select(.type == "toolCall") | .name // empty' 2>/dev/null | head -1)
if [ -n "$_tcid" ] && [ -n "$_tcname" ]; then
    pass "toolCall block has id and name"
else
    fail "toolCall block has id and name" "id=$_tcid name=$_tcname"
fi
if printf '%s' "$_tc_entry" | jq -e '.message.content[] | select(.type == "toolCall") | (.arguments | type) == "object"' >/dev/null 2>&1; then
    pass "toolCall arguments is a parsed object"
else
    fail "toolCall arguments is a parsed object"
fi

# The tool-call assistant entry carries stopReason toolUse
_tc_stop=$(printf '%s' "$_tc_entry" | jq -r '.message.stopReason // empty' 2>/dev/null)
if [ "$_tc_stop" = "toolUse" ]; then
    pass "tool-call assistant entry stopReason is toolUse"
else
    fail "tool-call assistant entry stopReason is toolUse" "got: $_tc_stop"
fi

# Each toolResult entry: toolCallId matches SOME toolCall of the turn,
# toolName matches, isError is a boolean, content is a text block
_tcids=$(sed -n '2,$p' "$_sj" | jq -r 'select(.message.role == "assistant") | .message.content[]? | select(.type == "toolCall") | .id' 2>/dev/null | tr '\n' ' ')
_tcnames=$(sed -n '2,$p' "$_sj" | jq -r 'select(.message.role == "assistant") | .message.content[]? | select(.type == "toolCall") | .name' 2>/dev/null | tr '\n' ' ')
sed -n '2,$p' "$_sj" | jq -c 'select(.message.role == "toolResult")' 2>/dev/null > "$SCRATCH/tr.jsonl"
_tr_ok=1
_tr_err=""
while IFS= read -r _trline; do
    _trcid=$(printf '%s' "$_trline" | jq -r '.message.toolCallId // empty' 2>/dev/null)
    _trname=$(printf '%s' "$_trline" | jq -r '.message.toolName // empty' 2>/dev/null)
    _iserr=$(printf '%s' "$_trline" | jq -r '.message.isError' 2>/dev/null)
    _trblock=$(printf '%s' "$_trline" | jq -r '.message.content[0].type // empty' 2>/dev/null)
    case " $_tcids " in
        *" $_trcid "*) ;;
        *) _tr_ok=0 ;;
    esac
    case " $_tcnames " in
        *" $_trname "*) ;;
        *) _tr_ok=0 ;;
    esac
    [ "$_iserr" = "true" ] || [ "$_iserr" = "false" ] || _tr_ok=0
    [ "$_trblock" = "text" ] || _tr_ok=0
    [ "$_tr_ok" -eq 1 ] || { _tr_err="toolCallId=$_trcid toolName=$_trname isError=$_iserr block=$_trblock"; break; }
done < "$SCRATCH/tr.jsonl"
if [ "$_tr_ok" -eq 1 ]; then
    pass "toolResult entries match the toolCall (id, name, isError, text block)"
else
    fail "toolResult entries match the toolCall (id, name, isError, text block)" "$_tr_err"
fi

# The final entry is an assistant message with stopReason stop
_final=$(tail -1 "$_sj")
_frole=$(printf '%s' "$_final" | jq -r '.message.role // empty' 2>/dev/null)
_fstop=$(printf '%s' "$_final" | jq -r '.message.stopReason // empty' 2>/dev/null)
if [ "$_frole" = "assistant" ] && [ "$_fstop" = "stop" ]; then
    pass "final entry is assistant with stopReason stop"
else
    fail "final entry is assistant with stopReason stop" "role=$_frole stop=$_fstop"
fi

# Ask jb to write a file and then read it back (all inside the scratch)
_out=$(echo "Write the text 'hello from jb test' to test_write.txt, then read it back to confirm. Reply with the content you read." | "$JB" run 2>/dev/null)

case "$_out" in
    *hello*from*jb*test*) pass "jb uses write+read tools" ;;
    *)                     fail "jb uses write+read tools" "got: $(echo "$_out" | head -c 300)" ;;
esac
