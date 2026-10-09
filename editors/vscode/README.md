# core-lang — Core language support for VS Code

Uses `tools/lsp/core_lsp.py` (stdlib-only Python LSP server).

## Manual install (until published to the Marketplace)

```sh
cp -r editors/vscode ~/.vscode/extensions/core-lang-0.1.0
# set "core-lang.serverPath" to <repo>/tools/lsp/core_lsp.py
# set "core-lang.coreBin" to your `core` binary
```

## Features

- diagnostics from `core check` on every edit
- hover for keywords, primitive types, `say`/`alloc`/`free`/`assert`
- completion for keywords, types, stdlib modules, local `func` names
- document symbols (`func`/`struct`/`class`/`enum`/`interface`/`trait`)
- syntax highlighting for `.cr` files (comment, string, keyword, type, number)
