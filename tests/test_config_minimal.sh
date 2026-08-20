# test_config_minimal.sh — the minimal config contract (P10)
#
# The config is exactly two required keys: api_url and model. Everything
# else (turn ceiling, token budget, output truncation) is a hardcoded
# constant, and legacy keys are rejected as unknown with a warning.
#
# Coverage:
#   1. unknown config-file keys warn (legacy names included)
#   2. unknown -c keys warn
#   3. -c api_url / model overrides apply (both directions, snapshot)
#   4. missing api_url or model -> exit 1 with a bootstrap hint
#   5. -c with missing '=' is skipped

repo_init

# ---- 1. unknown config-file keys warn (legacy names warn too) ----
_legacy='{
  "api_url": "'"$(sed -n 's/.*"api_url"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$SCRATCH/.config/jb/config.json" | head -1)"'",
  "model": "'"$(sed -n 's/.*"model"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$SCRATCH/.config/jb/config.json" | head -1)"'",
  "max_tokens": 500000,
  "token_budget": 500000,
  "temperature": 0.7,
  "max_turns": 10
}'
printf '%s\n' "$_legacy" > "$SCRATCH/.jb/config.json"
_err=$(echo "hi" | "$JB" run 2>&1 >/dev/null)
for k in max_tokens token_budget temperature max_turns; do
    case "$_err" in
        *"unknown key"*"$k"*) pass "legacy key $k warns as unknown" ;;
        *) fail "legacy key $k warns as unknown" "stderr: ${_err:0:100}" ;;
    esac
done

rm -f "$SCRATCH/.jb/config.json"

# ---- 2. unknown -c key warns ----
_err2=$(echo "hi" | "$JB" -c fly=faster run 2>&1 >/dev/null)
case "$_err2" in
    *"unknown key"*"fly"*) pass "unknown -c key warns" ;;
    *) fail "unknown -c key warns" "stderr: ${_err2:0:100}" ;;
esac

# ---- 3. -c overrides apply for the two real keys (snapshot proof) ----
_unique_model="minimal-test-model"
_out3=$(echo "say OK" | "$JB" -c model="$_unique_model" run 2>/dev/null)
_latest3=$(newest_session)
_m3=$(cat "$_latest3/metadata.json" 2>/dev/null)
if printf '%s' "$_m3" | jq -e ".config.model == \"$_unique_model\"" >/dev/null 2>&1; then
    pass "-c model override lands in the snapshot"
else
    fail "-c model override lands in the snapshot" "$(printf '%s' "$_m3" | jq -c '.config' 2>/dev/null)"
fi
if [ "$(jq -r '.config.model // empty' "$SCRATCH/.jb/config.json" 2>/dev/null)" = "" ]; then
    pass "-c override never persists into the local config"
else
    fail "-c override never persists into the local config"
fi

# ---- 4. missing api_url or model -> exit 1 + hint ----
_missing="$SCRATCH/missing.json"
printf '{"model":"%s"}\n' "$(sed -n 's/.*"model"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$SCRATCH/.config/jb/config.json" | head -1)" > "$_missing"
_out4=$(echo "hi" | "$JB" --config "$_missing" run 2>&1 >/dev/null)
_rc4=$?
if [ "$_rc4" -eq 1 ] && printf '%s' "$_out4" | grep -q "api_url and model are required"; then
    pass "missing api_url exits 1 with the bootstrap hint"
else
    fail "missing api_url exits 1 with the bootstrap hint" "exit $_rc4 stderr: ${_out4:0:120}"
fi

# ---- 5. -c without '=' is skipped (not a crash) ----
echo "say OK" | "$JB" -c model run >/dev/null 2>&1
_rc5=$?
if [ "$_rc5" -eq 0 ] || [ "$_rc5" -eq 1 ]; then
    pass "-c without = is skipped cleanly"
else
    fail "-c without = is skipped cleanly" "exit $_rc5"
fi
# ---- 6. empty tool-call batch terminates (regression: used to spin to the
#         50-turn ceiling when the provider sent finish tool_calls with no
#         tool_calls array entries) ----
if command -v python3 >/dev/null 2>&1; then
    mkdir -p "$SCRATCH/empty"
    cat > "$SCRATCH/empty/stub.py" <<'PYEOF'
import http.server
PORT = 17851
class H(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        self.rfile.read(n)
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()
        p = '{"choices":[{"index":0,"delta":{},"finish_reason":"tool_calls"}]}'
        self.wfile.write(b"data: " + p.encode() + b"\n\n")
        self.wfile.write(b"data: [DONE]\n\n")
    def log_message(self, *a): pass
http.server.HTTPServer(("127.0.0.1", PORT), H).serve_forever()
PYEOF
    python3 "$SCRATCH/empty/stub.py" &
    _epid=$!
    sleep 1
    _ecfg="$SCRATCH/empty.json"
    printf '{"api_url":"http://127.0.0.1:17851","model":"%s"}\n' \
        "$(sed -n 's/.*"model"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$SCRATCH/.config/jb/config.json" | head -1)" > "$_ecfg"
    echo "hi" | "$JB" run --config "$_ecfg" >/dev/null 2>&1
    kill "$_epid" 2>/dev/null
    _latestE=$(newest_session)
    _mE=$(cat "$_latestE/metadata.json" 2>/dev/null)
    _tE=$(printf '%s' "$_mE" | jq -r '.turns // 0' 2>/dev/null)
    if [ "$_tE" = "1" ]; then
        pass "empty tool-call batch ends the run (turns=$_tE, not 50)"
    else
        fail "empty tool-call batch ends the run" "turns=$_tE"
    fi
else
    echo "  SKIP: empty tool-call batch test (python3 not available)"
fi
