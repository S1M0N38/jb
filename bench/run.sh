#!/usr/bin/env bash
# bench/run.sh — measure peak memory of jb vs pi on the same prompt/model
set -eo pipefail
cd "$(dirname "$0")/.."

PROMPT="What is 2+2? Reply with just the number."
MODEL="zai/glm-5.1"
PI_FLAGS="--no-session -p --no-extensions --no-skills --no-themes --no-prompt-templates --no-context-files --offline --model $MODEL --thinking off"
RUNS=3

# Peak RSS of a PID and its direct children
peak_rss() {
    { ps -o rss= -p "$1" 2>/dev/null; pgrep -P "$1" 2>/dev/null | xargs ps -o rss= -p 2>/dev/null; } \
        | awk '{s+=$1} END {printf "%d", s+0}'
}

bench() {
    local name=$1
    shift
    local peak=0

    for run in $(seq 1 $RUNS); do
        "$@" >/tmp/jb-bench-out.txt 2>/dev/null &
        local pid=$!
        local run_peak=0

        while kill -0 "$pid" 2>/dev/null; do
            local r=$(peak_rss "$pid")
            [ "$r" -gt "$run_peak" ] && run_peak=$r
            sleep 0.1
        done
        wait "$pid" 2>/dev/null

        echo "  run $run: ${run_peak}KB"
        [ "$run_peak" -gt "$peak" ] && peak=$run_peak
    done

    printf "  => peak across %d runs: %dKB (%.1fMB)\n\n" "$RUNS" "$peak" "$(echo "$peak/1024" | bc -l)"
}

echo ""
echo "model: $MODEL"
echo "prompt: $PROMPT"
echo "runs: $RUNS"
echo ""

bench jb  sh -c "echo \"$PROMPT\" | ./jb"
bench pi  pi $PI_FLAGS "$PROMPT"
