# jb vs pi — Memory Benchmark

**Date:** 2025-05-25
**Prompt:** `What is 2+2? Reply with just the number.`
**Model:** zai/glm-5.1 (same for both)
**Method:** Peak RSS (process + children) sampled every 100ms, best of 3 runs.
**pi flags:** `--no-session --no-extensions --no-skills --no-themes --no-prompt-templates --no-context-files --offline --thinking off`

## Results

| Agent | Run 1    | Run 2    | Run 3    | Peak       |
|-------|----------|----------|----------|------------|
| jb    | 8096 KB  | 8128 KB  | 8112 KB  | **7.9 MB** |
| pi    | 187152 KB| 194592 KB| 193856 KB| **190.0 MB** |

**jb uses ~24x less memory than pi** for the same task on the same model.
