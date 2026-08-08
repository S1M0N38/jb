#!/bin/sh
# jb test runner — shell-based integration tests
# Usage: ./tests/run.sh
# Each test_*.sh is sourced into its own scratch dir; use assert_* helpers.
# Isolation: tests/run.sh sources tests/lib.sh and calls new_scratch() before
# every test file, so each test_*.sh runs in a fresh scratch dir with
# HOME/XDG_CONFIG_HOME/XDG_CACHE_HOME/TMPDIR redirected into it. Nothing a
# test does can touch the real ~/.cache, ~/.config, or the repo. Scratch
# dirs are removed on exit unless TEST_KEEP=1.

# NOTE: do NOT use set -e — tests must capture non-zero exit codes

PASS=0
FAIL=0
TOTAL=0

pass() { PASS=$((PASS + 1)); TOTAL=$((TOTAL + 1)); echo "  PASS: $1"; }
fail() { FAIL=$((FAIL + 1)); TOTAL=$((TOTAL + 1)); echo "  FAIL: $1 ${2:+— $2}"; }

assert_exit() {
    # assert_exit EXPECTED_EXITCODE DESCRIPTION COMMAND [ARGS...]
    _expected="$1"; shift
    _desc="$1"; shift
    "$@" >/dev/null 2>&1; _actual=$?
    if [ "$_actual" -eq "$_expected" ]; then
        pass "$_desc"
    else
        fail "$_desc" "expected exit $_expected, got $_actual"
    fi
}

assert_stdout_contains() {
    # assert_stdout_contains DESCRIPTION SUBSTRING COMMAND [ARGS...]
    _desc="$1"; shift
    _substr="$1"; shift
    _out=$("$@" 2>/dev/null) || true
    case "$_out" in
        *"$_substr"*) pass "$_desc" ;;
        *) fail "$_desc" "stdout did not contain: $_substr" ;;
    esac
}

assert_file_exists() {
    _desc="$1"; shift
    _file="$1"; shift
    if [ -f "$_file" ]; then
        pass "$_desc"
    else
        fail "$_desc" "file not found: $_file"
    fi
}

assert_dir_exists() {
    _desc="$1"; shift
    _dir="$1"; shift
    if [ -d "$_dir" ]; then
        pass "$_desc"
    else
        fail "$_desc" "dir not found: $_dir"
    fi
}

skip() { PASS=$((PASS + 1)); TOTAL=$((TOTAL + 1)); echo "  SKIP: $1 ${2:+— $2}"; }

# Repo-absolute paths: tests cd into per-test scratch dirs, so every repo
# reference must be absolute (use $REPO_ROOT/...).
REPO_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
JB="$REPO_ROOT/jb"

# Scratch isolation helpers (new_scratch, repo_init, newest_session,
# session_dir, prompt_pong) — sourced before any test file.
. "$REPO_ROOT/tests/lib.sh"

# Remove all scratch dirs on exit unless TEST_KEEP=1
if [ "$TEST_KEEP" != "1" ]; then
    trap 'for _s in $SCRATCHES; do [ -n "$_s" ] && rm -rf "$_s"; done' EXIT
fi

run_test_file() {
    # Fresh scratch per test file: isolated env, cwd = scratch
    new_scratch
    echo ""
    echo "--- $(basename "$1") ---"
    . "$1"
}

# Support running specific tests: make test-TESTNAME
if [ -n "$TEST_FILTER" ]; then
    echo "=== jb test suite (filter: $TEST_FILTER) ==="
    for t in "$REPO_ROOT"/tests/test_${TEST_FILTER}.sh; do
        [ -f "$t" ] || { echo "No test matching: test_${TEST_FILTER}.sh" >&2; exit 1; }
        run_test_file "$t"
    done
else
    echo "=== jb test suite ==="
    for t in "$REPO_ROOT"/tests/test_*.sh; do
        run_test_file "$t"
    done
fi

echo ""
echo "=== Results: $PASS passed, $FAIL failed, $TOTAL total ==="
[ "$FAIL" -eq 0 ]
