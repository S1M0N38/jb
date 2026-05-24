#!/bin/sh
# jb test runner — shell-based integration tests
# Usage: ./tests/run.sh
# Each test_*.sh is sourced; use assert_* helpers.

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
    "$@" >/dev/null 2>&/null; _actual=$?
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

JB="./jb"

echo "=== jb test suite ==="
for t in tests/test_*.sh; do
    echo ""
    echo "--- $(basename "$t") ---"
    . "$t"
done

echo ""
echo "=== Results: $PASS passed, $FAIL failed, $TOTAL total ==="
[ "$FAIL" -eq 0 ]
