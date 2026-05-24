# Pure stdin filter with no flags or arguments

jb reads its prompt from stdin and writes the model's final answer to stdout. There are no CLI flags, no arguments, no interactive mode, no `-v`/`-q`/`--model` options. All configuration comes from `config.json` and environment variables.

This makes jb a pure Unix filter — composable with pipes, heredocs, and subshells. A sub-agent is just `echo "task" | jb` inside a bash tool call. The cost is convenience: to change the model, you edit config.json; to see progress, you `tail -f` the session log. Every other coding agent (pi, Claude Code, Codex) chose interactive TUI with flags. jb chose `stdin → agent loop → stdout` because composability with other Unix tools matters more than human-first terminal UX.
