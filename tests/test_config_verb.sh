# test_config_verb.sh — jb config [--global|--local] [KEY [VALUE]] (§7)
# Git-style: list effective settings (local merged over global, sorted),
# print a key, set a key. Values stored as strings, coerced at use.
# Per-run overrides: jb -c KEY=VALUE run — never persisted.

# global file: deterministic contents in the scratch (overwrites the
# copied real config — the scratch copy is ours to write)
cat > "$XDG_CONFIG_HOME/jb/config.json" <<'EOF'
{
  "api_url": "https://api.example.com/v1",
  "model": "global-model",
  "max_tokens": 100
}
EOF
repo_init   # creates .jb/config.json = {}

# ---- list: local merged over global, sorted ----
_out=$("$JB" config 2>/dev/null)
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "jb config exits 0"
else
    fail "jb config exits 0" "got $_rc"
fi
if [ "$(printf '%s\n' "$_out" | sed -n '1p')" = "api_url=https://api.example.com/v1" ]; then
    pass "jb config lists the global api_url first (sorted)"
else
    fail "jb config lists the global api_url first (sorted)" "got: $(echo "$_out" | head -1)"
fi
case "$_out" in
    *"max_tokens=100"*"model=global-model"*) pass "jb config lists global settings" ;;
    *) fail "jb config lists global settings" "got: $_out" ;;
esac

# ---- get: falls through to the global file (local untouched so far) ----
if [ "$("$JB" config api_url 2>/dev/null)" = "https://api.example.com/v1" ]; then
    pass "jb config KEY falls through to the global file"
else
    fail "jb config KEY falls through to the global file" "got: $("$JB" config api_url 2>/dev/null)"
fi

# ---- set local (default) ----
"$JB" config model local-model >/dev/null 2>&1
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "jb config KEY VALUE sets locally"
else
    fail "jb config KEY VALUE sets locally" "got $_rc"
fi
_m=$(jq -r '.model // empty' ".jb/config.json" 2>/dev/null)
if [ "$_m" = "local-model" ]; then
    pass "jb config wrote the local file"
else
    fail "jb config wrote the local file" "got: $_m"
fi
# existing local keys are preserved (two sets into the same local file)
"$JB" config api_url https://local.example.com/v1 >/dev/null 2>&1
if jq -e '.api_url == "https://local.example.com/v1" and .model == "local-model"' ".jb/config.json" >/dev/null 2>&1; then
    pass "jb config preserves existing local keys"
else
    fail "jb config preserves existing local keys" "got: $(cat .jb/config.json 2>/dev/null)"
fi

# ---- list: local wins ----
_out=$("$JB" config 2>/dev/null)
case "$_out" in
    *"model=local-model"*) pass "jb config local overrides global in the list" ;;
    *) fail "jb config local overrides global in the list" "got: $_out" ;;
esac

# ---- get: local value wins; other keys fall through to global ----
if [ "$("$JB" config model 2>/dev/null)" = "local-model" ]; then
    pass "jb config KEY prints the effective value"
else
    fail "jb config KEY prints the effective value" "got: $("$JB" config model 2>/dev/null)"
fi

# ---- missing key ----
_err=$("$JB" config no-such-key 2>&1 >/dev/null)
_rc=$?
case "$_err" in
    *"no such key"*) pass "jb config <unknown key> reports no such key" ;;
    *) fail "jb config <unknown key> reports no such key" "got: $_err" ;;
esac
if [ "$_rc" -eq 1 ]; then
    pass "jb config <unknown key> exits 1"
else
    fail "jb config <unknown key> exits 1" "got $_rc"
fi

# ---- values are stored as strings, coerced at use ----
"$JB" config max_tokens 5000 >/dev/null 2>&1
_t=$(jq -r '.max_tokens | type' ".jb/config.json" 2>/dev/null)
if [ "$_t" = "string" ]; then
    pass "jb config stores values as strings"
else
    fail "jb config stores values as strings" "got type: $_t"
fi

# ---- --global ----
"$JB" config --global model gm2 >/dev/null 2>&1
_rc=$?
if [ "$_rc" -eq 0 ]; then
    pass "jb config --global KEY VALUE sets the global file"
else
    fail "jb config --global KEY VALUE sets the global file" "got $_rc"
fi
if [ "$(jq -r '.model // empty' "$XDG_CONFIG_HOME/jb/config.json" 2>/dev/null)" = "gm2" ]; then
    pass "jb config --global wrote the XDG file"
else
    fail "jb config --global wrote the XDG file"
fi
_out=$("$JB" config --global 2>/dev/null)
case "$_out" in
    *"model=gm2"*) pass "jb config --global lists only the global file" ;;
    *) fail "jb config --global lists only the global file" "got: $_out" ;;
esac
# effective view still has the local override
if [ "$("$JB" config model 2>/dev/null)" = "local-model" ]; then
    pass "jb config effective view keeps the local override"
else
    fail "jb config effective view keeps the local override"
fi

# ---- outside a repo: list/get work, set (local) is refused ----
new_scratch
cat > "$XDG_CONFIG_HOME/jb/config.json" <<'EOF'
{
  "api_url": "https://api.example.com/v1",
  "model": "gm2"
}
EOF
_out=$("$JB" config 2>/dev/null)
_rc=$?
case "$_out" in
    *"model=gm2"*) pass "jb config lists global settings outside a repo" ;;
    *) fail "jb config lists global settings outside a repo" "got: $_out" ;;
esac
if [ "$_rc" -eq 0 ]; then
    pass "jb config list outside a repo exits 0"
else
    fail "jb config list outside a repo exits 0" "got $_rc"
fi
"$JB" config model x >/dev/null 2>&1
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "jb config set (local) outside a repo exits 1"
else
    fail "jb config set (local) outside a repo exits 1" "got $_rc"
fi

# ---- usage: too many arguments ----
cd "$SCRATCH"
repo_init >/dev/null 2>&1
"$JB" config a b c >/dev/null 2>&1
_rc=$?
if [ "$_rc" -eq 2 ]; then
    pass "jb config with too many arguments exits 2"
else
    fail "jb config with too many arguments exits 2" "got $_rc"
fi

# ---- -c KEY=VALUE override on run (never persisted) ----
_out=$(prompt_pong | "$JB" -c max_tokens=77777 run 2>/dev/null)
_rc=$?
_latest=$(newest_session)
if [ -n "$_latest" ] && [ -f "$_latest/metadata.json" ]; then
    _mt=$(jq -r '.config.max_tokens // empty' "$_latest/metadata.json" 2>/dev/null)
    if [ "$_mt" = "77777" ]; then
        pass "jb -c overrides the run config (metadata snapshot)"
    else
        fail "jb -c overrides the run config (metadata snapshot)" "got: $_mt"
    fi
else
    fail "jb -c overrides the run config (metadata snapshot)" "no session dir found (exit $_rc)"
fi
if [ "$(jq -r '.config.max_tokens // empty' ".jb/config.json" 2>/dev/null)" = "" ]; then
    pass "jb -c never persists into the local config"
else
    fail "jb -c never persists into the local config"
fi
# unknown -c keys are silently ignored
_out2=$(prompt_pong | "$JB" -c bogus-key=1 run 2>/dev/null)
if [ "$(jq -r '.config.model // empty' "$(newest_session)metadata.json" 2>/dev/null)" != "" ]; then
    pass "jb -c with an unknown key is ignored"
else
    fail "jb -c with an unknown key is ignored"
fi
