# test_seed.sh — --seed provenance, $JB_SESSION env lineage, stale-env guard
# (implementation plan phase 4: --seed provenance; @ from $JB_SESSION;
# stale env → "jb: JB_SESSION … not found", exit 1)

repo_init

# Source session for provenance (the creator in the assertions below)
prompt_pong | "$JB" run >/dev/null 2>&1
_src=$(newest_session)
_src_uuid=$(basename "$_src")

# ---- Slice 1: --seed sets author (provenance), never parent ----

prompt_pong | "$JB" run --seed "$_src_uuid" >/dev/null 2>&1
_seeded=$(newest_session)
_seeded_uuid=$(basename "$_seeded")

if [ "$_seeded_uuid" != "$_src_uuid" ]; then
    pass "--seed run creates its own session"
else
    fail "--seed run creates its own session" "same uuid as source"
fi

_sa=$(jq -r '.author // empty' "$_seeded/metadata.json" 2>/dev/null)
if [ "$_sa" = "$_src_uuid" ]; then
    pass "--seed records author (provenance)"
else
    fail "--seed records author (provenance)" "got: $_sa"
fi

_sp=$(jq 'has("parent")' "$_seeded/metadata.json" 2>/dev/null)
if [ "$_sp" = "false" ]; then
    pass "--seed does not set parent (provenance only)"
else
    fail "--seed does not set parent (provenance only)" "parent present: $_sp"
fi

# ---- Slice 2: --seed @ resolves $JB_SESSION ----

prompt_pong | env JB_SESSION="$_src_uuid" "$JB" run --seed @ >/dev/null 2>&1
_latest=$(newest_session)
_a=$(jq -r '.author // empty' "$_latest/metadata.json" 2>/dev/null)
if [ "$_a" = "$_src_uuid" ]; then
    pass "--seed @ resolves the env session as author"
else
    fail "--seed @ resolves the env session as author" "got: $_a"
fi

# ---- Slice 3: env lineage (no flags) — author from $JB_SESSION ----

prompt_pong | env JB_SESSION="$_src_uuid" "$JB" run >/dev/null 2>&1
_latest=$(newest_session)
_a=$(jq -r '.author // empty' "$_latest/metadata.json" 2>/dev/null)
if [ "$_a" = "$_src_uuid" ]; then
    pass "spawned session records the env author"
else
    fail "spawned session records the env author" "got: $_a"
fi
_p=$(jq 'has("parent")' "$_latest/metadata.json" 2>/dev/null)
if [ "$_p" = "false" ]; then
    pass "env lineage does not set parent"
else
    fail "env lineage does not set parent" "parent present: $_p"
fi

# ---- Slice 4: stale $JB_SESSION is rejected before any session is made ----

_ns_before=$(ls "$JB_SESSIONS_DIR" | wc -l | tr -d ' ')
_stale="00000000-0000-0000-0000-000000000000"
echo "hi" | env JB_SESSION="$_stale" "$JB" run >/dev/null 2>"$SCRATCH/stale.err"
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass 'stale $JB_SESSION exits 1'
else
    fail 'stale $JB_SESSION exits 1' "got exit $_rc"
fi
case "$(cat "$SCRATCH/stale.err")" in
    *"jb: JB_SESSION $_stale not found"*) pass 'stale $JB_SESSION error message' ;;
    *) fail 'stale $JB_SESSION error message' "got: $(cat "$SCRATCH/stale.err")" ;;
esac
_ns_after=$(ls "$JB_SESSIONS_DIR" | wc -l | tr -d ' ')
if [ "$_ns_after" = "$_ns_before" ]; then
    pass "stale env creates no session"
else
    fail "stale env creates no session" "$_ns_before → $_ns_after dirs"
fi

# ---- Slice 5: --seed with an unresolvable id ----

echo "hi" | "$JB" run --seed deadbeef00 >/dev/null 2>"$SCRATCH/badseed.err"
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "--seed unknown session exits 1"
else
    fail "--seed unknown session exits 1" "got exit $_rc"
fi
case "$(cat "$SCRATCH/badseed.err")" in
    *"jb: no session 'deadbeef00'"*) pass "--seed unknown session error message" ;;
    *) fail "--seed unknown session error message" "got: $(cat "$SCRATCH/badseed.err")" ;;
esac

# ---- Slice 6: --seed @ without env ----

echo "hi" | env -u JB_SESSION "$JB" run --seed @ >/dev/null 2>"$SCRATCH/seedat.err"
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "--seed @ without env exits 1"
else
    fail "--seed @ without env exits 1" "got exit $_rc"
fi
case "$(cat "$SCRATCH/seedat.err")" in
    *"jb: JB_SESSION not set"*) pass "--seed @ without env error message" ;;
    *) fail "--seed @ without env error message" "got: $(cat "$SCRATCH/seedat.err")" ;;
esac
