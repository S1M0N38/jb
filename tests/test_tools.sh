# test_tools.sh — Goal 5-6: agentic loop with tool calls

# The scratch cwd is empty; copy a known repo file in so ls/read have a
# stable target that the assertions can rely on.
cp "$REPO_ROOT/CONTEXT.md" .

# Ask jb to list files (should use bash tool)
_out=$(echo "run ls on the current directory and tell me what files you see. Reply with just the filenames, one per line." | "$JB" 2>/dev/null)

# Check that the response mentions CONTEXT.md (a file we know exists)
case "$_out" in
    *CONTEXT.md*) pass "jb uses bash tool to list files" ;;
    *)            fail "jb uses bash tool to list files" "got: $(echo "$_out" | head -c 300)" ;;
esac

# Ask jb to read a file (should use read tool)
_out=$(echo "Read the file CONTEXT.md and tell me the first line after the heading. Reply with just that line." | "$JB" 2>/dev/null)

case "$_out" in
    *minimal*agentic* | *jb* | *A*minimal*) pass "jb uses read tool to read file" ;;
    *) fail "jb uses read tool to read file" "got: $(echo "$_out" | head -c 300)" ;;
esac

# Ask jb to write a file and then read it back (all inside the scratch)
_out=$(echo "Write the text 'hello from jb test' to test_write.txt, then read it back to confirm. Reply with the content you read." | "$JB" 2>/dev/null)

case "$_out" in
    *hello*from*jb*test*) pass "jb uses write+read tools" ;;
    *)                     fail "jb uses write+read tools" "got: $(echo "$_out" | head -c 300)" ;;
esac
