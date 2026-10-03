# The `core.toml` Manifest

Every Core project and package has a `core.toml` at its root. It is the
project's identity card and build recipe. The parser is a small TOML
subset: `[table]` headers, `key = "string"`, arrays of strings,
booleans, integers, `#` comments, and `[[array-of-tables]]` (used only
by `core.lock`).

---

## 1. Full example

```toml
# Core project manifest - see docs/packages/manifest.md
[package]
name = "myproject"              # required; also the binary name
version = "0.3.1"               # your package's version (default "0.1.0")
core-version = ">=0.1.0"        # compiler version requirement
source-dir = "src"              # where build/run find main.cr (default "src")

[build]
link = ["m", "curl"]            # native libs -> -lcurl -lm
link-paths = ["vendor/lib"]     # -> -Lvendor/lib
link-args = ["-Wl,-rpath,$ORIGIN/vendor/lib"]

[dependencies]
coolmath = "github.com/snitch/coolmath@^1.2.0"    # shorthand form
json = { git = "github.com/user/json", version = ">=2.0.0" }
mylib = { path = "../mylib" }                     # local path dependency
```

---

## 2. `[package]`

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `name` | string | — (required in practice) | Project/package name. `core build` and `core run` produce and run a binary with exactly this name; `core init` derives it from the directory name. |
| `version` | string | `"0.1.0"` | This package's version. When you publish, make it match the git tag (publishing.md §3). |
| `core-version` | string | `""` (any) | Declared compiler requirement, e.g. `">=0.1.0"`. Parsed and recorded; **not enforced** by the toolchain in v1. |
| `source-dir` | string | `"src"` | Directory of the entry file. `core build`/`run`/`test` compile `<source-dir>/main.cr` as the entry; the directory is on the import path. |

## 3. `[build]`

All three keys are arrays of strings; all are optional. They are added
to the final system-linker invocation (`cc`) in this order:

1. `link` — each entry becomes `-l<entry>` (native libraries).
2. `link-paths` — each entry becomes `-L<entry>`.
3. `link-args` — each entry is passed through verbatim.

```toml
[build]
link = ["sqlite3"]
link-paths = ["/opt/sqlite/lib"]
link-args = ["-Wl,-rpath,/opt/sqlite/lib"]
```

Order across the dependency graph: your own `[build]` settings come
last; each dependency's `[build]` settings are appended before yours,
in manifest order. The compiler always links its runtime first
(`corert.o -lm -lpthread -ldl -latomic`).

For standalone compiles (`core compile <out> <file.cr>`) the same
settings are provided as flags: `--link=<lib>`, `--lib-path=<dir>`,
`--link-arg=<arg>`.

## 4. `[dependencies]`

Each key is the dependency's **import-facing name** (what you type in
`core remove`, and what `core list` shows); the value says where to get
it and what version is acceptable.

### 4.1 Shorthand (string) form

```toml
[dependencies]
coolmath = "github.com/snitch/coolmath@^1.2.0"
```

Format: `"<repo>[@<constraint>]"`.

- `repo` is `github.com/user/repo` (shorthand for the HTTPS git URL),
  any other git URL, or a local path.
- `constraint` is a semver requirement: `^1.2.0`, `>=1.0.0`, or an
  exact `1.2.3` (see publishing.md §4 for the matching rules).
- Omitting `@constraint` means "any version" (highest tag, or HEAD if
  no tags).

### 4.2 Table form

```toml
[dependencies]
json  = { git = "github.com/user/json", version = ">=2.0.0" }
mylib = { path = "../mylib" }
```

- `git` — repository (same forms as `repo` above).
- `version` — constraint string.
- `path` — local directory used directly as the checkout (no clone, no
  copy). Takes precedence for local development; a path dep's `commit`
  is read from its own git repo when present, else recorded as
  `"local"`.

A path dependency's *value* key in the table form is `path`; in the
shorthand form, a bare path also works
(`coolmath = "/tmp/mockrepo/coolmath@"` — as `core install` writes it).

## 5. Where the manifest is read

- `core build` / `run` / `test` / `check` / `list` / `install` /
  `remove` / `update` require a `core.toml` in the current directory
  (error "no core.toml in this directory - run 'core init' first" —
  standalone files use `core compile` instead).
- `core build` compiles `<source-dir>/main.cr` and names the output
  binary after `[package] name`.
- `core init [name]` writes a template manifest (commented), an empty
  `core.lock`, `src/main.cr`, `tests/`, `examples/`, `assets/`, and a
  README — and refuses to overwrite an existing `core.toml`.

## 6. Caveats

- The TOML subset does not support inline tables spanning lines, dotted
  keys (`a.b = 1`), or multi-line strings. Keep values on one line.
- `core-version` is not validated in v1; put your real requirement in
  your README as well.
- Dependency keys are cosmetic-but-real: `core remove <name>` matches
  the key, not the repository.
