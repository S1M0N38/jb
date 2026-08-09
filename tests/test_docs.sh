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

# ---- Phase 9: docs match the implementation reference (v2 · pi-compatible) ----

# Rendered man page text (macOS: mandoc; fallback: raw source) — the seam is
# what a user actually reads, not the roff macros.
if command -v mandoc >/dev/null 2>&1; then
    _man=$(mandoc "$REPO_ROOT/jb.1" 2>/dev/null | col -b)
else
    _man=$(cat "$REPO_ROOT/jb.1")
fi

# jb.1 documents the 12 verbs of the command reference (§7)
for _verb in init run commit status log show ps wait path export config help; do
    case "$_man" in
        *"jb $_verb"*) pass "jb.1 documents jb $_verb" ;;
        *) fail "jb.1 documents jb $_verb" "not found" ;;
    esac
done

# jb.1 documents the repo-scoped storage layout with the three session files
case "$_man" in
    *".jb/sessions"*"session.jsonl"*) pass "jb.1 documents .jb/sessions/session.jsonl" ;;
    *) fail "jb.1 documents .jb/sessions/session.jsonl" "not found" ;;
esac
case "$_man" in
    *"events.jsonl"*) pass "jb.1 documents events.jsonl" ;;
    *) fail "jb.1 documents events.jsonl" "not found" ;;
esac
case "$_man" in
    *"metadata.json"*) pass "jb.1 documents metadata.json" ;;
    *) fail "jb.1 documents metadata.json" "not found" ;;
esac

# jb.1 must not document the deleted pre-phase-2 surface
for _old in '$XDG_CACHE_HOME' 'state.jsonl' 'log.jsonl' '--parent'; do
    case "$_man" in
        *"$_old"*) fail "jb.1 has no $_old" "still present" ;;
        *) pass "jb.1 has no $_old" ;;
    esac
done

# jb.1 documents JB_SESSION and the -C/-c global flags (§7)
case "$_man" in
    *"JB_SESSION"*) pass "jb.1 documents JB_SESSION" ;;
    *) fail "jb.1 documents JB_SESSION" "not found" ;;
esac
case "$_man" in
    *"-C DIR"*) pass "jb.1 documents -C DIR" ;;
    *) fail "jb.1 documents -C DIR" "not found" ;;
esac
case "$_man" in
    *"-c KEY=VALUE"*) pass "jb.1 documents -c KEY=VALUE" ;;
    *) fail "jb.1 documents -c KEY=VALUE" "not found" ;;
esac

# CONTEXT.md must match the current design — no pre-phase-2 storage language
for _old in '$XDG_CACHE_HOME' '--parent' '"running"'; do
    case "$(cat "$REPO_ROOT/CONTEXT.md")" in
        *"$_old"*) fail "CONTEXT.md has no $_old" "still present" ;;
        *) pass "CONTEXT.md has no $_old" ;;
    esac
done

# The deleted wire-format files may appear only as deleted (ADR-0006)
for _old in state.jsonl log.jsonl; do
    _bad=$(grep -n "$_old" "$REPO_ROOT/CONTEXT.md" | grep -v deleted || true)
    if [ -z "$_bad" ]; then
        pass "CONTEXT.md mentions $_old only as deleted"
    else
        fail "CONTEXT.md mentions $_old only as deleted" "found: $_bad"
    fi
done

# CONTEXT.md documents the pi-format conversation record (v3)
case "$(cat "$REPO_ROOT/CONTEXT.md")" in
    *"session.jsonl"*"version"*3*|*"version 3"*|*"v3"*) pass "CONTEXT.md documents session.jsonl pi format v3" ;;
    *) fail "CONTEXT.md documents session.jsonl pi format v3" "not found" ;;
esac

# ADR-0006: storage v2 — pi format, repo-scoped
if [ -f "$REPO_ROOT/docs/adr/0006-storage-v2-pi-format.md" ]; then
    pass "ADR-0006 exists"
    for _kw in "pi format" "repo-scoped"; do
        case "$(cat "$REPO_ROOT/docs/adr/0006-storage-v2-pi-format.md")" in
            *"$_kw"*) pass "ADR-0006 mentions $_kw" ;;
            *) fail "ADR-0006 mentions $_kw" "not found" ;;
        esac
    done
else
    fail "ADR-0006 exists" "docs/adr/0006-storage-v2-pi-format.md missing"
fi

# ADRs documenting the deleted design are marked superseded (point at 0006)
for _adr in 0001-local-conversation-state 0004-session-metadata 0005-jb-tool-and-session-tree; do
    case "$(cat "$REPO_ROOT/docs/adr/$_adr.md")" in
        *"superseded"*|*"Superseded"*|*"SUPERSEDED"*) pass "$_adr is marked superseded" ;;
        *) fail "$_adr is marked superseded" "no superseded marker" ;;
    esac
done

# README points at the repo-scoped world (jb init / .jb / jb run)
case "$(cat "$REPO_ROOT/README")" in
    *"jb init"*|*".jb"*) pass "README mentions the jb repository" ;;
    *) fail "README mentions the jb repository" "not found" ;;
esac
case "$(cat "$REPO_ROOT/README")" in
    *"$XDG_CACHE_HOME"*) fail "README has no XDG_CACHE_HOME references" ;;
    *) pass "README has no XDG_CACHE_HOME references" ;;
esac
