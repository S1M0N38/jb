# pi-export — vendored pi viewer assets

Source: `@earendil-works/pi-coding-agent@0.84.1`
`dist/core/export-html/` — **pinned**, do not edit in place.

- `template.html` — viewer skeleton (`{{CSS}} {{JS}} {{MARKED_JS}}
  {{HIGHLIGHT_JS}} {{SESSION_DATA}}` placeholders)
- `template.css` — viewer stylesheet (`{{THEME_VARS}} {{BODY_BG}}
  {{CONTAINER_BG}} {{INFO_BG}}` placeholders)
- `template.js` — viewer app (vanilla JS)
- `vendor/marked.min.js` — markdown → HTML
- `vendor/highlight.min.js` — syntax highlighting
- `theme-vars.css` — the resolved default (dark) theme custom properties,
  generated once from pi's own theme code (see below) — jb has no TUI
  themes, so the default dark theme is hardcoded per reference §8.3.

## Licenses

- pi's viewer code (`template.html/css/js`, `marked.min.js`): **MIT**
  (see pi's package.json license field).
- `highlight.min.js`: **BSD-3-Clause** (banner preserved in the file).
- The export embeds these assets byte-for-byte; no license text is
  embedded in the HTML output.

## Build dependency

`gen-assets.sh` uses `xxd` (part of vim, present on macOS/BSD/Linux by
default in practice) to turn the assets into C arrays; the Makefile runs
it before compiling `export.o`. `src/assets.inc` is a generated artifact
(gitignored, removed by `make clean`).

## Re-vendor (upgrade path)

```sh
# 1. copy the five assets from a newer pi install
SRC=.../node_modules/@earendil-works/pi-coding-agent/dist/core/export-html
cp $SRC/template.html $SRC/template.css $SRC/template.js src/vendor/pi-export/
cp $SRC/vendor/marked.min.js $SRC/vendor/highlight.min.js src/vendor/pi-export/vendor/

# 2. regenerate theme-vars.css from that pi's own resolver
node - <<'EOF'
import { getResolvedThemeColors } from ".../dist/modes/interactive/theme/theme.js";
const colors = getResolvedThemeColors("dark");
const lines = Object.entries(colors).map(([k, v]) => `--${k}: ${v};`);
lines.push("--exportPageBg: #18181e;");
lines.push("--exportCardBg: #1e1e24;");
lines.push("--exportInfoBg: #3c3728;");
process.stdout.write(lines.join("\n      ") + "\n");
EOF

# 3. bump the pin above, rebuild (make clean; make)
```
