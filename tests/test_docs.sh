# test_docs.sh — man page, README, version.h

# Man page exists
assert_file_exists "jb.1 exists" "$REPO_ROOT/jb.1"

# README exists
assert_file_exists "README exists" "$REPO_ROOT/README"

# version.h exists
assert_file_exists "src/version.h exists" "$REPO_ROOT/src/version.h"

# Man page renders without errors (mandoc on macOS)
if command -v mandoc >/dev/null 2>&1; then
    _out=$(mandoc "$REPO_ROOT/jb.1" 2>&1)
    _rc=$?
    if [ "$_rc" -eq 0 ] && [ -n "$_out" ]; then
        pass "man page renders with mandoc"
    else
        fail "man page renders with mandoc" "exit $_rc"
    fi
else
    pass "man page renders (skipped — no mandoc)"
fi

# Man page contains key sections
for _section in NAME SYNOPSIS DESCRIPTION "EXIT STATUS" EXAMPLES BUGS "SEE ALSO"; do
    case "$(cat "$REPO_ROOT/jb.1")" in
        *"$_section"*) pass "man page has $_section section" ;;
        *) fail "man page has $_section section" "not found" ;;
    esac
done

# Man page .TH line has version
case "$(cat "$REPO_ROOT/jb.1")" in
    *"jb 0.1"*) pass "man page .TH includes version" ;;
    *) fail "man page .TH includes version" ;;
esac

# Makefile has install target
case "$(cat "$REPO_ROOT/Makefile")" in
    *"install:"*) pass "Makefile has install target" ;;
    *) fail "Makefile has install target" ;;
esac

# Makefile has PREFIX
case "$(cat "$REPO_ROOT/Makefile")" in
    *"PREFIX"*) pass "Makefile defines PREFIX" ;;
    *) fail "Makefile defines PREFIX" ;;
esac
