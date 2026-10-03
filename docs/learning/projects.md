# Projects

Beyond single files: manifests, lockfiles, the build/test/check workflow, and the compiler flags that matter for real binaries.

## Anatomy

```
myapp/
├── core.toml          # the manifest — source of truth for the build
├── core.lock          # resolved dependency versions (tool-managed)
├── README.md
├── src/
│   ├── main.cr        # entry: contains func main()
│   └── ...            # other modules
├── tests/             # tests/*.cr, each with test_* funcs + func main()
├── examples/          # scratch programs
└── assets/            # data files
```

## core.toml

```toml
[package]
name = "myapp"              # also the output binary name
version = "0.1.0"
core-version = ">=0.1.0"    # compiler compatibility constraint
source-dir = "src"          # where core build finds main.cr

[build]
link = ["m"]                # native libs: -lm (see FFI)
link-paths = []             # -L paths
link-args = []              # raw linker args

[dependencies]
# coolmath = "github.com/user/coolmath@^1.2.0"
# coolmath = "/path/to/local/pkg"        (local path dependency)
```

## core.lock

Records the exact resolved version, commit, and sha256 of every dependency. Managed by `core install/remove/update` — don't edit while builds run. Commit it: it makes builds reproducible. An empty project's lockfile simply has no entries.

## The command set

| Command | What it does |
|---|---|
| `core init [name]` | scaffold a project (refuses to overwrite existing files) |
| `core build` | compile `<source-dir>/main.cr` → `./<name>` using the manifest |
| `core run [args]` | build, then run; extra args pass to the binary |
| `core test` | compile + run each `tests/*.cr` file, report `N/M test files passed` |
| `core check` | type-check only — fast, no codegen |
| `core compile <out> <entry.cr> [flags]` | compile a specific file to a named binary |
| `core install/remove/update/list` | package management (see [packages.md](packages.md)) |
| `core emit-ir <f>` / `core emit-asm <f>` | inspect generated LLVM IR / assembly |
| `core version` | compiler + backend version |

```bash
$ core run
built: myapp
Hello, Core!
```

## Build repetition and outputs

- `core compile` **won't overwrite** an existing output binary without `--force`.
- A failed compile can leave `<out>.coreobj.o` behind (the object half of a failed link); delete it or pass `--force` when recompiling.
- `core build` manages the project binary name for you; inside projects prefer it over raw `compile`.

## Compiler flags (with `core compile`)

```bash
core compile myapp src/main.cr -O2          # optimize (default -O0)
core compile myapp src/main.cr -Os          # optimize for size
core compile myapp src/main.cr --debug      # DWARF debug info for GDB/LLDB
core compile myapp src/main.cr -O2 --debug  # both (unusual but legal)

core compile obj main.cr --emit-object      # stop at <obj>.coreobj.o (ELF object)
core compile arm main.cr --target=aarch64 --emit-object   # cross-compile to object

core compile myapp src/main.cr --link=m --lib-path=/opt/lib --link-arg=-Wl,-rpath,/opt/lib
```

- **Optimization**: `-O0` fast builds; `-O2` for release; `-O3` aggressive; `-Os` small.
- **`--debug`**: emits DWARF; GDB breakpoints resolve to `.cr` lines (`gdb ./myapp`, `break main`).
- **`--target=x86_64|aarch64|riscv64`**: emits a relocatable object for that architecture; linking needs a target toolchain (no bundled cross-linkers).
- **`--freestanding`**: no runtime/libc, custom entry — see [freestanding-development.md](freestanding-development.md).
- **`--link`/`--link-arg`/`--lib-path`**: linker control — also settable in `core.toml [build]` so `core build` picks them up.

## Testing convention

Each `tests/*.cr` file is compiled as its own program: define `test_*` functions and a `main` that calls them, failing via `assert`:

```core
// tests/test_math.cr
import math

func test_sqrt() {
    assert(math.sqrt(16.0) == 4.0, "sqrt 16")
}

func test_abs() {
    assert(math.abs(-3 as i32) == 3, "abs -3")
}

func main() {
    test_sqrt()
    test_abs()
    say "all ok"
}
```

```bash
$ core test
built: core_test_test_math
all ok
---- running tests/test_math.cr

1/1 test files passed
```

## Complete working example

```bash
core init demo && cd demo
# replace src/main.cr with:
#   import math
#   func main() {
#       for i in 1..=5 { say to_string(i) + "^2 = " + to_string(i * i) }
#   }
core build     # built: demo
./demo
core check     # clean
core test      # no tests found (add tests/*.cr)
```

## Common mistakes

- **Running project commands outside the project root.** `core build/run/test/check` all need `core.toml` in the current directory.
- **Renaming the binary by hand.** It's `<package.name>` from the manifest; change the manifest instead.
- **Committing binaries.** Add `demo`, `*.coreobj.o`, `core_test_*` to `.gitignore`.
- **Expecting incremental builds.** v0.1 recompiles the whole program each time; keep modules lean (see [modules.md](modules.md)).
- **`link` entries without the library installed** — the system linker fails with `cannot find -l<name>`; check `link-paths` too.

## Performance notes

- Release builds: `-O2` minimum; benchmark `-O3` vs `-Os` for your workload.
- `--debug` binaries are fine to ship internally — the DWARF overhead is modest; strip for release.
- Build time scales with total program size (whole-program model); `core check` is the fast inner loop while iterating on types.

## When to use / not use

- **Projects** for anything with a dependency, tests, or more than one file.
- **Bare `core compile`** for one-file experiments and learning (like most examples in these tutorials).
- Put demos in `examples/` with their own structure only if they're standalone teaching material — the compiler only builds `src/main.cr` via `core build`.

## Exercises

1. Run the init → build → run flow of the complete example, then rename the project in `core.toml` and confirm the new binary name.
2. Add a deliberately failing assert to a `tests/*.cr` file and run `core test` — read the failure output, then fix it.
3. Compile with `-O0` and `-Os` and compare binary sizes; add both invocations as comments in your README.
4. Use `core check` while introducing a type error (`say "age: " + 42`) and confirm no binary is produced.
5. Create a `.gitignore` covering the binary, `*.coreobj.o`, and `core_test_*` artifacts.

Next: [Packages](packages.md).
