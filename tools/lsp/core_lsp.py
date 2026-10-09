#!/usr/bin/env python3
"""core-lsp: minimal Language Server Protocol server for Core (.cr files).

Stdlib only, speaks LSP over stdio (Content-Length framed JSON-RPC).

    python3 tools/lsp/core_lsp.py          # started by the editor
    core-lsp --check /path/to/core         # override the compiler binary

Capabilities:
  - textDocument/didOpen, didChange, didClose (full-sync)
  - textDocument/diagnostic via `core check <file>` (pulled on save/change)
  - textDocument/hover (keywords + `say`, `alloc`, `free` quick docs)
  - textDocument/completion (keywords, primitive types, stdlib modules, local funcs)
  - textDocument/documentSymbol (func/struct/class/enum/interface names)
"""
import json
import os
import re
import subprocess
import sys

KEYWORDS = ("func struct class interface trait enum import extern pub private "
            "static mut const abstract virtual override tls if else while for in "
            "break continue return switch case default match loop as unsafe true "
            "false null self super and or sizeof alignof").split()
TYPES = ("void never bool char string i8 i16 i32 i64 i128 u8 u16 u32 u64 u128 "
         "f32 f64 usize isize f32x4 f64x2 i32x4 i64x2 i8x16 i16x8 u8x16 u16x8 "
         "u32x4 u64x2").split()
BUILTINS = {"say": "print a value + newline (prelude)",
            "alloc": "alloc<T>() — heap-allocate, must free() (std/memory)",
            "free": "free(ptr) — release memory from alloc()",
            "assert": "assert(cond[, msg]) — fail test / abort on false"}
STDLIB = ["prelude", "memory", "math", "thread", "time", "process", "simd"]
DOCS = {}
for k in KEYWORDS:
    DOCS[k] = f"Core keyword `{k}`"
for t in TYPES:
    DOCS[t] = f"Core primitive type `{t}`"
DOCS.update(BUILTINS)

CORE_BIN = os.environ.get("CORE_BIN", "core")
for a in sys.argv[1:]:
    if a.startswith("--check="):
        CORE_BIN = a.split("=", 1)[1]

documents = {}


def read_msg():
    headers = {}
    while True:
        line = sys.stdin.buffer.readline().decode("ascii", "replace").strip()
        if not line:
            break
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip().lower()] = v.strip()
    length = int(headers.get("content-length", 0))
    if not length:
        return None
    return json.loads(sys.stdin.buffer.read(length).decode("utf-8"))


def send(msg):
    body = json.dumps(msg).encode()
    sys.stdout.buffer.write(b"Content-Length: %d\r\n\r\n" % len(body))
    sys.stdout.buffer.write(body)
    sys.stdout.buffer.flush()


def run_check(path, text):
    """Type-check via `core check`. Write buffer to tmp file (unsaved edits)."""
    import tempfile
    suffix = ".cr"
    with tempfile.NamedTemporaryFile("w", suffix=suffix, delete=False) as f:
        f.write(text)
        tmp = f.name
    try:
        p = subprocess.run([CORE_BIN, "check", tmp], capture_output=True,
                           text=True, timeout=20)
        out = (p.stdout or "") + (p.stderr or "")
    except Exception as e:
        return [{"range": {"start": {"line": 0, "character": 0},
                           "end": {"line": 0, "character": 1}},
                 "severity": 1, "source": "core",
                 "message": f"core check failed: {e}"}]
    finally:
        try:
            os.unlink(tmp)
        except OSError:
            pass
    diags = []
    for line in out.splitlines():
        m = re.search(r"(?:error|warning).*?(\d+):(\d+)", line)
        sev = 1 if "error" in line.lower() else 2
        ln, col = (int(m.group(1)) - 1, int(m.group(2)) - 1) if m else (0, 0)
        # map tmp-file diagnostics onto line 0 if unmappable; still surfaces text
        diags.append({"range": {"start": {"line": max(ln, 0),
                                          "character": max(col, 0)},
                                "end": {"line": max(ln, 0),
                                        "character": max(col, 0) + 1}},
                      "severity": sev, "source": "core", "message": line.strip()})
    return diags


def word_at(text, line, ch):
    lines = text.splitlines()
    if line >= len(lines):
        return ""
    s = lines[line]
    m = [m for m in re.finditer(r"[A-Za-z_][A-Za-z0-9_]*", s)
         if m.start() <= ch <= m.end()]
    return m[-1].group(0) if m else ""


def symbols_of(text):
    out = []
    for i, line in enumerate(text.splitlines()):
        m = re.match(r"\s*(?:pub\s+)?(func|struct|class|interface|trait|enum)\s+([A-Za-z_]\w*)", line)
        if m:
            kind = {"func": 12, "struct": 23, "class": 5,
                    "interface": 11, "trait": 11, "enum": 10}[m.group(1)]
            out.append({"name": m.group(2), "kind": kind,
                        "location": {"uri": "", "range": {
                            "start": {"line": i, "character": 0},
                            "end": {"line": i, "character": len(line)}}}})
    return out


def handle(msg):
    method = msg.get("method", "")
    mid = msg.get("id")
    params = msg.get("params", {}) or {}

    def reply(result):
        send({"jsonrpc": "2.0", "id": mid, "result": result})

    def notify(m, p):
        send({"jsonrpc": "2.0", "method": m, "params": p})

    if method == "initialize":
        reply({"capabilities": {
            "textDocumentSync": 1,
            "hoverProvider": True,
            "completionProvider": {"triggerCharacters": [".", ":"]},
            "documentSymbolProvider": True,
            "diagnosticProvider": {"interFileDependencies": False,
                                   "workspaceDiagnostics": False}},
            "serverInfo": {"name": "core-lsp", "version": "0.1.0"}})
    elif method == "initialized":
        pass
    elif method == "shutdown":
        reply(None)
    elif method == "exit":
        sys.exit(0)
    elif method == "textDocument/didOpen":
        doc = params["textDocument"]
        documents[doc["uri"]] = doc["text"]
    elif method == "textDocument/didChange":
        uri = params["textDocument"]["uri"]
        documents[uri] = params["contentChanges"][-1]["text"]
        diags = run_check(uri, documents[uri])
        notify("textDocument/publishDiagnostics",
               {"uri": uri, "diagnostics": diags})
    elif method == "textDocument/didClose":
        documents.pop(params["textDocument"]["uri"], None)
    elif method == "textDocument/hover":
        uri = params["textDocument"]["uri"]
        pos = params["position"]
        w = word_at(documents.get(uri, ""), pos["line"], pos["character"])
        doc = DOCS.get(w)
        reply({"contents": {"kind": "markdown", "value": doc}} if doc
              else None)
    elif method == "textDocument/completion":
        uri = params["textDocument"]["uri"]
        text = documents.get(uri, "")
        items = [{"label": k, "kind": 14} for k in KEYWORDS + TYPES]
        items += [{"label": s, "kind": 9} for s in STDLIB]
        for m in re.finditer(r"func\s+([A-Za-z_]\w*)", text):
            items.append({"label": m.group(1), "kind": 3})
        reply({"isIncomplete": False, "items": items})
    elif method == "textDocument/documentSymbol":
        uri = params["textDocument"]["uri"]
        syms = symbols_of(documents.get(uri, ""))
        for s in syms:
            s["location"]["uri"] = uri
        reply(syms)
    elif mid is not None:
        reply(None)


def main():
    while True:
        msg = read_msg()
        if msg is None:
            break
        try:
            handle(msg)
        except Exception as e:
            if "id" in msg and msg["id"] is not None:
                send({"jsonrpc": "2.0", "id": msg["id"],
                      "error": {"code": -32603, "message": str(e)}})


if __name__ == "__main__":
    main()
