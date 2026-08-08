# test_edit.sh — Goal 6: edit tool

# Create a test file in the scratch cwd, then ask jb to edit it
echo "Hello World" > edit_target.txt

_out=$(echo "Edit the file edit_target.txt: replace 'World' with 'jb'. Reply with the final content." | "$JB" 2>/dev/null)

case "$_out" in
    *Hello*jb*) pass "jb uses edit tool to modify file" ;;
    *)          fail "jb uses edit tool to modify file" "got: $(echo "$_out" | head -c 300)" ;;
esac

# Verify the file was actually edited
_content=$(cat edit_target.txt)
case "$_content" in
    *Hello*jb*) pass "edit tool actually modified the file" ;;
    *)          fail "edit tool actually modified the file" "got: $_content" ;;
esac
