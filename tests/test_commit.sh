# test_commit.sh — jb commit ID [-m subject] [-m body] (plan phase 7)
# The deliberate act: lifecycle rules (refuse working, refuse uncommitted
# parent, -m/-m, amend), message-generation failure aborts (session stays
# completed), immutability (commit changes metadata only — session.jsonl
# byte-identical before/after). Auto message (real API) and the committed
# forest (--graph kinds + [error] marker) live in slices 2/3 below.

repo_init

_a="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
_b="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
_c="cccccccccccccccccccccccccccccccc"
_e="eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"
_p="dddddddddddddddddddddddddddddddd"

# ---- usage & gating ----

assert_exit 2 "jb commit without an ID exits 2" "$JB" commit

new_scratch   # no repo here — repo_init ran in the previous scratch
assert_exit 1 "jb commit outside a repo exits 1" "$JB" commit "$_a"

_out=$("$JB" commit "$_a" 2>&1)
case "$_out" in
    *"not a jb repository"*) pass "jb commit outside a repo prints the fatal" ;;
    *) fail "jb commit outside a repo prints the fatal" "got: $_out" ;;
esac

repo_init     # back into a repository for the rule tests

assert_exit 1 "jb commit on a missing session exits 1" "$JB" commit 99999999

# ---- refuse working ----

fixture_session "$_a" working "working A"
_err=$("$JB" commit "$_a" 2>&1 >/dev/null)
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "jb commit refuses a working session"
else
    fail "jb commit refuses a working session" "got exit $_rc"
fi
case "$_err" in
    "jb: cannot commit aaaaaaaa — status is working"*) pass "jb commit working refusal message" ;;
    *) fail "jb commit working refusal message" "got: $_err" ;;
esac

# ---- refuse uncommitted parent ----

fixture_session "$_p" completed "parent P" "" "" 200
fixture_session "$_c" completed "child C" "$_p" "$_p" 100
_err=$("$JB" commit "$_c" -m "subj" 2>&1 >/dev/null)
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "jb commit refuses a session whose parent is uncommitted"
else
    fail "jb commit refuses a session whose parent is uncommitted" "got exit $_rc"
fi
case "$_err" in
    "jb: cannot commit cccccccc — parent dddddddd is not committed"*)
        pass "jb commit uncommitted-parent message" ;;
    *) fail "jb commit uncommitted-parent message" "got: $_err" ;;
esac

# ---- commit a completed session with -m/-m (no API) ----

fixture_session "$_b" completed "completed B" "" "" 120
cp "$(session_dir "$_b")/session.jsonl" "$SCRATCH/before.jsonl"
_out=$("$JB" commit "$_b" -m "subject B" -m "body B" 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "jb commit on a completed session exits 0"
else
    fail "jb commit on a completed session exits 0" "got exit $_rc"
fi
if [ "$_out" = 'committed bbbbbbbb  "subject B"' ]; then
    pass "jb commit prints committed <id>  \"<subject>\""
else
    fail "jb commit prints committed <id>  \"<subject>\"" "got: $_out"
fi

_m=$(cat "$(session_dir "$_b")/metadata.json")
if [ "$(printf '%s' "$_m" | jq -r '.status')" = "committed" ]; then
    pass "jb commit sets status committed"
else
    fail "jb commit sets status committed" "got: $(printf '%s' "$_m" | jq -r '.status')"
fi
if [ "$(printf '%s' "$_m" | jq -r '.subject')" = "subject B" ]; then
    pass "jb commit writes the -m subject"
else
    fail "jb commit writes the -m subject" "got: $(printf '%s' "$_m" | jq -r '.subject')"
fi
if [ "$(printf '%s' "$_m" | jq -r '.body')" = "body B" ]; then
    pass "jb commit writes the second -m as body"
else
    fail "jb commit writes the second -m as body" "got: $(printf '%s' "$_m" | jq -r '.body')"
fi
if cmp -s "$SCRATCH/before.jsonl" "$(session_dir "$_b")/session.jsonl"; then
    pass "jb commit leaves session.jsonl byte-identical"
else
    fail "jb commit leaves session.jsonl byte-identical"
fi

# commit preserves the other metadata fields (config snapshot, turns…)
if [ "$(printf '%s' "$_m" | jq -r '.turns')" = "1" ] \
   && [ "$(printf '%s' "$_m" | jq -r '.config.model')" = "gpt-4.1" ]; then
    pass "jb commit preserves the rest of the metadata"
else
    fail "jb commit preserves the rest of the metadata"
fi

# ---- amend: re-committing a committed session replaces subject/body ----

cp "$(session_dir "$_b")/session.jsonl" "$SCRATCH/before2.jsonl"
_out=$("$JB" commit "$_b" -m "amended subject" 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ] && [ "$_out" = 'committed bbbbbbbb  "amended subject"' ]; then
    pass "jb commit amends a committed session (replaces subject)"
else
    fail "jb commit amends a committed session (replaces subject)" "exit $_rc, out: $_out"
fi
_m=$(cat "$(session_dir "$_b")/metadata.json")
if [ "$(printf '%s' "$_m" | jq -r '.subject')" = "amended subject" ] \
   && [ "$(printf '%s' "$_m" | jq -r '.body')" = "" ]; then
    pass "jb commit amend resets the body without a second -m"
else
    fail "jb commit amend resets the body without a second -m"
fi
if cmp -s "$SCRATCH/before2.jsonl" "$(session_dir "$_b")/session.jsonl"; then
    pass "jb commit amend leaves session.jsonl byte-identical"
else
    fail "jb commit amend leaves session.jsonl byte-identical"
fi

# ---- committed parent is committable ----

"$JB" commit "$_p" -m "parent P" >/dev/null 2>&1
_out=$("$JB" commit "$_c" -m "child C" 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ] && [ "$_out" = 'committed cccccccc  "child C"' ]; then
    pass "jb commit allows a child once its parent is committed"
else
    fail "jb commit allows a child once its parent is committed" "exit $_rc, out: $_out"
fi

# ---- error sessions are committable ----

fixture_session "$_e" error "failed E" "" "" 80 1
_out=$("$JB" commit "$_e" -m "release notes draft" 2>/dev/null)
if [ $? -eq 0 ] && [ "$_out" = 'committed eeeeeeee  "release notes draft"' ]; then
    pass "jb commit accepts an error session"
else
    fail "jb commit accepts an error session" "out: $_out"
fi

# ---- -m usage errors ----

assert_exit 2 "jb commit rejects a third -m" "$JB" commit "$_b" -m a -m b -m c
assert_exit 2 "jb commit rejects -m without a value" "$JB" commit "$_b" -m
assert_exit 2 "jb commit rejects unknown options" "$JB" commit "$_b" -x foo
assert_exit 2 "jb commit rejects extra positionals" "$JB" commit "$_b" "$_b"

# ---- generation failure aborts; session stays completed ----
# (dead local api_url forces the API error path — no retries in commit)

new_scratch
repo_init
fixture_session "$_b" completed "completed B" "" "" 120
printf '{"api_url":"http://127.0.0.1:1/v1"}\n' > .jb/config.json
_err=$("$JB" commit "$_b" 2>&1 >/dev/null)
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "jb commit aborts when message generation fails"
else
    fail "jb commit aborts when message generation fails" "got exit $_rc"
fi
case "$_err" in
    *"jb: cannot commit bbbbbbbb — message generation failed"*)
        pass "jb commit generation-failure message" ;;
    *) fail "jb commit generation-failure message" "got: $_err" ;;
esac
if [ "$(cat "$(session_dir "$_b")/metadata.json" | jq -r '.status')" = "completed" ]; then
    pass "jb commit generation failure leaves the session completed"
else
    fail "jb commit generation failure leaves the session completed"
fi

# ---- slice 2: auto message (real API) — subject/body generated ----
# The generation prompt explicitly forbids obeying instructions inside the
# conversation (and gives a few-shot example), so even degenerate
# "reply with exactly X" conversations yield the JSON summary (phase 9
# hardening). prompt_pong is the canonical suite prompt.

new_scratch
repo_init
if echo "$(prompt_pong)" | "$JB" run >/dev/null 2>/dev/null; then
    pass "jb run completes (slice 2 setup)"
else
    fail "jb run completes (slice 2 setup)"
fi
_id=$(basename "$(newest_session)")
_id8=$(printf '%.8s' "$_id")
cp "$(session_dir "$_id")/session.jsonl" "$SCRATCH/before.jsonl"
cp "$(session_dir "$_id")/events.jsonl" "$SCRATCH/before.events"

JB_SESSION="$_id" "$JB" commit @ >"$SCRATCH/commit.out" 2>/dev/null
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "jb commit @ auto-generates the message (real API)"
else
    fail "jb commit @ auto-generates the message (real API)" "got exit $_rc"
fi
_out=$(cat "$SCRATCH/commit.out")
case "$_out" in
    "committed $_id8  "*""*) pass "jb commit @ prints committed <id>  \"<subject>\"" ;;
    *) fail "jb commit @ prints committed <id>  \"<subject>\"" "got: $_out" ;;
esac
_subj=$(sed -n "s/^committed $_id8  \"\(.*\)\"$/\1/p" "$SCRATCH/commit.out")
if [ -n "$_subj" ]; then
    pass "jb commit auto-generates a non-empty subject"
else
    fail "jb commit auto-generates a non-empty subject" "got: $_out"
fi

_m=$(cat "$(session_dir "$_id")/metadata.json")
if [ "$(printf '%s' "$_m" | jq -r '.status')" = "committed" ]; then
    pass "jb commit @ rewrites status to committed"
else
    fail "jb commit @ rewrites status to committed"
fi
if [ "$(printf '%s' "$_m" | jq -r '.subject')" = "$_subj" ]; then
    pass "jb commit @ writes the generated subject to metadata"
else
    fail "jb commit @ writes the generated subject to metadata" \
        "meta: $(printf '%s' "$_m" | jq -r '.subject')"
fi
if [ "$(printf '%s' "$_m" | jq -r '.body | type')" = "string" ]; then
    pass "jb commit @ records a body field"
else
    fail "jb commit @ records a body field" "body: $(printf '%s' "$_m" | jq -r '.body | type')"
fi
if cmp -s "$SCRATCH/before.jsonl" "$(session_dir "$_id")/session.jsonl" \
   && cmp -s "$SCRATCH/before.events" "$(session_dir "$_id")/events.jsonl"; then
    pass "jb commit @ leaves session.jsonl and events.jsonl byte-identical"
else
    fail "jb commit @ leaves session.jsonl and events.jsonl byte-identical"
fi

# amend on the real session: -m replaces the generated subject
JB_SESSION="$_id" "$JB" commit @ -m "amended subject" >/dev/null 2>&1
_m=$(cat "$(session_dir "$_id")/metadata.json")
if [ "$(printf '%s' "$_m" | jq -r '.subject')" = "amended subject" ]; then
    pass "jb commit amend replaces the generated subject"
else
    fail "jb commit amend replaces the generated subject"
fi
if cmp -s "$SCRATCH/before.jsonl" "$(session_dir "$_id")/session.jsonl"; then
    pass "jb commit amend keeps session.jsonl byte-identical"
else
    fail "jb commit amend keeps session.jsonl byte-identical"
fi

# the committed root appears in the graph with kind root
if "$JB" log --graph 2>/dev/null | grep -q "^$_id8  root  "; then
    pass "jb log --graph shows the committed root"
else
    fail "jb log --graph shows the committed root" "$( "$JB" log --graph 2>/dev/null )"
fi

# ---- slice 3: the committed forest — fork/fresh kinds, [error] marker ----

# fork: continuation of the committed root
if echo "$(prompt_pong)" | "$JB" run --fork "$_id" >/dev/null 2>/dev/null; then
    pass "jb run --fork completes (slice 3 setup)"
else
    fail "jb run --fork completes (slice 3 setup)"
fi
_fid=$(basename "$(newest_session)")
_fid8=$(printf '%.8s' "$_fid")
"$JB" commit "$_fid" -m "fork subject" >/dev/null 2>&1
if "$JB" log --graph 2>/dev/null | grep -q "$_fid8  fork  \"fork subject\""; then
    pass "jb log --graph shows the committed fork under its root"
else
    fail "jb log --graph shows the committed fork under its root" \
        "$( "$JB" log --graph 2>/dev/null )"
fi

# fresh: authored by the root via $JB_SESSION, no --fork parent
if echo "$(prompt_pong)" | JB_SESSION="$_id" "$JB" run >/dev/null 2>/dev/null; then
    pass "jb run with JB_SESSION completes (slice 3 setup)"
else
    fail "jb run with JB_SESSION completes (slice 3 setup)"
fi
_sid=$(basename "$(newest_session)")
_sid8=$(printf '%.8s' "$_sid")
"$JB" commit "$_sid" -m "fresh subject" >/dev/null 2>&1
if "$JB" log --graph 2>/dev/null | grep -q "$_sid8  fresh  \"fresh subject\""; then
    pass "jb log --graph shows the committed fresh session under its author"
else
    fail "jb log --graph shows the committed fresh session under its author" \
        "$( "$JB" log --graph 2>/dev/null )"
fi

# [error] marker: a committed error session (fixture, no API)
_e8=$(printf '%.8s' "$_e")
fixture_session "$_e" error "failed E" "" "" 60 1
"$JB" commit "$_e" -m "release notes draft" >/dev/null 2>&1
if "$JB" log --graph 2>/dev/null | grep -q "$_e8  \[error\]  root  "; then
    pass "jb log --graph marks committed error sessions [error]"
else
    fail "jb log --graph marks committed error sessions [error]" \
        "$( "$JB" log --graph 2>/dev/null )"
fi
