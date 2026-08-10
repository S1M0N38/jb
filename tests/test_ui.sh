# test_ui.sh — jb ui: the read-only session forest viewer (localhost HTTP).
# Seam under test: the HTTP surface — start `jb ui --port 0` against a
# fixture repo and curl the routes; plus the CLI surface (exit codes).

# ---- CLI surface: argument errors (parsed before repo check) ----
assert_exit 2 "jb ui rejects unknown option" "$JB" ui --bogus
assert_exit 2 "jb ui rejects missing --port value" "$JB" ui --port
assert_exit 2 "jb ui rejects non-numeric --port" "$JB" ui --port abc
assert_exit 2 "jb ui rejects out-of-range --port" "$JB" ui --port 70000

# ---- CLI surface: not a repo ----
_out=$("$JB" ui --port 0 2>&1)
_rc=$?
if [ "$_rc" -eq 1 ]; then
    pass "jb ui outside a repo exits 1"
else
    fail "jb ui outside a repo exits 1" "got exit $_rc"
fi
case "$_out" in
    *"not a jb repository"*) pass "jb ui outside a repo names the fatal" ;;
    *) fail "jb ui outside a repo names the fatal" "got: $_out" ;;
esac

# ---- in-repo: fixtures ----
repo_init
_a="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
_b="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
_c="cccccccccccccccccccccccccccccccc"
_d="dddddddddddddddddddddddddddddddd"
fixture_session "$_a" committed "root A subject" "" "" 600
fixture_session "$_b" committed "fork B subject" "$_a" "$_a" 550
fixture_session "$_c" working "spawn C subject" "$_a" "" 500
fixture_session "$_d" committed "root D subject" "" "" 400

_UI_PID=""
_ui_start() {  # _ui_start [extra args...] — background jb ui, wait for URL
    "$JB" ui --port 0 "$@" > "$SCRATCH/ui.log" 2>&1 &
    _UI_PID=$!
    _url=""
    _i=0
    while [ $_i -lt 50 ]; do
        _url=$(grep -o 'http://127\.0\.0\.1:[0-9]*/' "$SCRATCH/ui.log" 2>/dev/null | head -1)
        [ -n "$_url" ] && break
        sleep 0.1
        _i=$((_i + 1))
    done
    if [ -z "$_url" ]; then
        fail "jb ui prints the serving URL" "log: $(cat "$SCRATCH/ui.log" 2>/dev/null)"
        return 1
    fi
    pass "jb ui prints the serving URL"
    return 0
}
_ui_stop() {  # stop the background server, reap it
    if [ -n "$_UI_PID" ]; then
        kill "$_UI_PID" 2>/dev/null
        wait "$_UI_PID" 2>/dev/null
        _UI_PID=""
    fi
}
trap '_ui_stop' EXIT

# ---- HTTP: static routes ----
if _ui_start; then
    _code=$(curl -s -o /dev/null -w '%{http_code}' "$_url")
    [ "$_code" = "200" ] && pass "GET / returns 200" || fail "GET / returns 200" "got $_code"
    _body=$(curl -s "$_url")
    case "$_body" in
        *"jb forest"*) pass "GET / serves the forest UI" ;;
        *) fail "GET / serves the forest UI" "body: $(printf '%s' "$_body" | head -c 120)" ;;
    esac
    _code=$(curl -s -o /dev/null -w '%{http_code}' "${_url}app.js")
    [ "$_code" = "200" ] && pass "GET /app.js returns 200" || fail "GET /app.js returns 200" "got $_code"
    _body=$(curl -s "${_url}app.js")
    case "$_body" in
        *"api/sessions"*) pass "GET /app.js serves the UI script" ;;
        *) fail "GET /app.js serves the UI script" ;;
    esac
    _code=$(curl -s -o /dev/null -w '%{http_code}' "${_url}style.css")
    [ "$_code" = "200" ] && pass "GET /style.css returns 200" || fail "GET /style.css returns 200" "got $_code"

    # ---- HTTP: /api/sessions ----
    _code=$(curl -s -o /dev/null -w '%{http_code}' "${_url}api/sessions")
    [ "$_code" = "200" ] && pass "GET /api/sessions returns 200" || fail "GET /api/sessions returns 200" "got $_code"
    _sess=$(curl -s "${_url}api/sessions")
    _n=$(printf '%s' "$_sess" | grep -o '"uuid"' | wc -l | tr -d ' ')
    [ "$_n" = "4" ] && pass "/api/sessions lists all 4 sessions" || fail "/api/sessions lists all 4 sessions" "got $_n"
    for _u in "$_a" "$_b" "$_c" "$_d"; do
        case "$_sess" in
            *"$_u"*) pass "/api/sessions carries fixture $_u" ;;
            *) fail "/api/sessions carries fixture $_u" "got: $(printf '%s' "$_sess" | head -c 200)" ;;
        esac
    done
    for _st in committed working; do
        case "$_sess" in
            *'"status":"'$_st'"'*) pass "/api/sessions carries status $_st" ;;
            *) fail "/api/sessions carries status $_st" "got: $(printf '%s' "$_sess" | head -c 200)" ;;
        esac
    done
    for _su in "root A subject" "spawn C subject"; do
        case "$_sess" in
            *"$_su"*) pass "/api/sessions carries subject: $_su" ;;
            *) fail "/api/sessions carries subject: $_su" "got: $(printf '%s' "$_sess" | head -c 200)" ;;
        esac
    done
    case "$_sess" in
        *'"author":"'$_a'"'*'"parent":"'$_a'"'*) pass "/api/sessions carries lineage (author/parent)" ;;
        *) fail "/api/sessions carries lineage (author/parent)" "got: $(printf '%s' "$_sess" | head -c 200)" ;;
    esac
    case "$_sess" in
        *'"started_ms":'*) pass "/api/sessions carries started_ms" ;;
        *) fail "/api/sessions carries started_ms" ;;
    esac
    case "$_sess" in
        *'"ended_ms":'*) pass "/api/sessions carries ended_ms for finished sessions" ;;
        *) fail "/api/sessions carries ended_ms for finished sessions" ;;
    esac
    # the working fixture (spawn C) must NOT carry ended_ms: extract its object
    _c_obj=$(printf '%s' "$_sess" | grep -o '"uuid":"'$_c'"[^}]*}')
    case "$_c_obj" in
        *'"ended_ms":'*) fail "/api/sessions omits ended_ms while working" "got: $_c_obj" ;;
        *) pass "/api/sessions omits ended_ms while working" ;;
    esac

    # ---- HTTP: /api/session/<id> ----
    _code=$(curl -s -o /dev/null -w '%{http_code}' "${_url}api/session/$_a")
    [ "$_code" = "200" ] && pass "GET /api/session/<id> returns 200" || fail "GET /api/session/<id> returns 200" "got $_code"
    _det=$(curl -s "${_url}api/session/$_a")
    for _frag in '"turns": 1' '"tokens_used": 10' '"working_dir"' '"root A subject"'; do
        case "$_det" in
            *"$_frag"*) pass "/api/session/<id> carries $_frag" ;;
            *) fail "/api/session/<id> carries $_frag" "got: $(printf '%s' "$_det" | head -c 200)" ;;
        esac
    done
    _code=$(curl -s -o /dev/null -w '%{http_code}' "${_url}api/session/deadbeefdeadbeefdeadbeefdeadbeef")
    [ "$_code" = "404" ] && pass "GET /api/session/<unknown> returns 404" || fail "GET /api/session/<unknown> returns 404" "got $_code"

    # ---- HTTP: unknown routes ----
    _code=$(curl -s -o /dev/null -w '%{http_code}' "${_url}nope")
    [ "$_code" = "404" ] && pass "unknown route returns 404" || fail "unknown route returns 404" "got $_code"
    _code=$(curl -s -o /dev/null -w '%{http_code}' "${_url}api/")
    [ "$_code" = "404" ] && pass "unknown api route returns 404" || fail "unknown api route returns 404" "got $_code"
    _code=$(curl -s -o /dev/null -w '%{http_code}' -X POST "${_url}api/sessions")
    [ "$_code" = "405" ] && pass "non-GET returns 405" || fail "non-GET returns 405" "got $_code"

    _ui_stop
fi

# ---- port already in use ----
"$JB" ui --port 0 > "$SCRATCH/ui.log" 2>&1 &
_P1=$!
_url1=""
_i=0
while [ $_i -lt 50 ]; do
    _url1=$(grep -o 'http://127\.0\.0\.1:[0-9]*/' "$SCRATCH/ui.log" 2>/dev/null | head -1)
    [ -n "$_url1" ] && break
    sleep 0.1
    _i=$((_i + 1))
done
if [ -n "$_url1" ]; then
    _p1=$(printf '%s' "$_url1" | sed 's|http://127\.0\.0\.1:||; s|/||')
    "$JB" ui --port "$_p1" > "$SCRATCH/ui2.log" 2>&1
    _rc=$?
    if [ "$_rc" -eq 1 ]; then
        pass "jb ui exits 1 when the port is taken"
    else
        fail "jb ui exits 1 when the port is taken" "got exit $_rc"
    fi
    case "$(cat "$SCRATCH/ui2.log")" in
        *"bind"*) pass "jb ui names the bind failure" ;;
        *) fail "jb ui names the bind failure" "got: $(cat "$SCRATCH/ui2.log")" ;;
    esac
    kill "$_P1" 2>/dev/null
    wait "$_P1" 2>/dev/null
else
    fail "first server for port-in-use test never started" "log: $(cat "$SCRATCH/ui.log")"
fi

# ---- --dev serves ui/ from the repo ----
mkdir -p "$SCRATCH/ui"
printf '<html><body>DEV-INDEX</body></html>\n' > "$SCRATCH/ui/index.html"
printf 'dev app.js\n' > "$SCRATCH/ui/app.js"
printf 'dev style.css\n' > "$SCRATCH/ui/style.css"
"$JB" ui --dev --port 0 > "$SCRATCH/ui.log" 2>&1 &
_UI_PID=$!
_url=""
_i=0
while [ $_i -lt 50 ]; do
    _url=$(grep -o 'http://127\.0\.0\.1:[0-9]*/' "$SCRATCH/ui.log" 2>/dev/null | head -1)
    [ -n "$_url" ] && break
    sleep 0.1
    _i=$((_i + 1))
done
if [ -n "$_url" ]; then
    _body=$(curl -s "$_url")
    case "$_body" in
        *"DEV-INDEX"*) pass "--dev serves ui/ from the repo root" ;;
        *) fail "--dev serves ui/ from the repo root" "got: $(printf '%s' "$_body" | head -c 120)" ;;
    esac
    _code=$(curl -s -o /dev/null -w '%{http_code}' "${_url}app.js")
    [ "$_code" = "200" ] && pass "--dev serves app.js from the repo root" || fail "--dev serves app.js from the repo root" "got $_code"
else
    fail "--dev server never started" "log: $(cat "$SCRATCH/ui.log")"
fi
_ui_stop
