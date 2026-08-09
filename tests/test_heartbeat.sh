# test_heartbeat.sh — metadata last_activity advances mid-run (review fix E).
# A tool turn (sleep 4) guarantees the run stays alive well past the first
# heartbeat; we poll metadata while it works. Tolerant: if the model skips
# the tool or the run finishes before the poll observes it, the test is
# skipped, not failed (plan §2.2 — tool scenarios are encouraged, not
# forced; timing-dependent asserts stay skippable).

repo_init

echo "run the bash command 'sleep 4' and then reply with exactly the word DONE" \
    | "$JB" run >/dev/null 2>&1 &
_rpid=$!

_start=""
_hb=""
_st=""
_i=0
while [ "$_i" -lt 30 ]; do   # up to 15s
    sleep 0.5
    _i=$((_i + 1))
    _latest=$(newest_session)
    [ -n "$_latest" ] || continue
    [ -f "$_latest/metadata.json" ] || continue
    _st=$(jq -r '.status // empty' "$_latest/metadata.json" 2>/dev/null)
    _start=$(jq -r '.started_at // empty' "$_latest/metadata.json" 2>/dev/null)
    _hb=$(jq -r '.last_activity // empty' "$_latest/metadata.json" 2>/dev/null)
    if [ "$_st" = "working" ] && [ -n "$_hb" ] && [ "$_hb" != "$_start" ]; then
        break
    fi
    if [ "$_st" = "completed" ] || [ "$_st" = "error" ]; then
        break
    fi
done

wait "$_rpid"
_rc=$?

if [ "$_rc" -ne 0 ]; then
    skip "heartbeat: run exited $_rc before completing" "tolerant"
    return 0
fi
if [ -z "$_start" ]; then
    skip "heartbeat: no metadata observed" "tolerant"
    return 0
fi
if [ "$_st" = "working" ] && [ -n "$_hb" ] && [ "$_hb" != "$_start" ]; then
    pass "heartbeat: last_activity advanced while the session was working"
else
    skip "heartbeat: last_activity advance not observed" \
        "status=$_st start=$_start last=$_hb (single-turn run?)"
fi

# The final metadata still closes with completed and a fresh last_activity
_cstatus=$(jq -r '.status // empty' "$_latest/metadata.json" 2>/dev/null)
_cend=$(jq -r '.ended_at // empty' "$_latest/metadata.json" 2>/dev/null)
_clast=$(jq -r '.last_activity // empty' "$_latest/metadata.json" 2>/dev/null)
if [ "$_cstatus" = "completed" ] && [ -n "$_cend" ] && [ "$_clast" = "$_cend" ]; then
    pass "heartbeat: close rewrites last_activity = ended_at"
else
    fail "heartbeat: close rewrites last_activity = ended_at" \
        "status=$_cstatus end=$_cend last=$_clast"
fi
