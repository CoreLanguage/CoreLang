# First project

This tutorial walks through the full workflow: scaffolding, building, running, and testing — then a tour of what each file is for.

## Create the project

```bash
core init demo
cd demo
```

`core init [name]` scaffolds a project and **refuses to overwrite** existing files. Without a name it uses the current directory's name.

## Project layout

```
demo/
├── core.toml          # manifest: name, version, dependencies, link settings
├── core.lock          # exact resolved dependency versions (tool-managed)
├── README.md
├── src/
│   └── main.cr        # entry point: contains func main()
├── tests/             # test_*.cr files, run with `core test`
├── examples/          # scratch programs
└── assets/            # data files (not touched by the compiler)
```

`core.toml` right after init:

```toml
[package]
name = "demo"
version = "0.1.0"
core-version = ">=0.1.0"
source-dir = "src"

[build]
# native libraries to link: link = ["m"]
link = []

[dependencies]
# example: coolmath = "github.com/snitch/coolmath@^1.2.0"
```

`src/main.cr` contains the classic hello:

```core
func main() {
    say "Hello, Core!"
}
```

## The four commands

```bash
core build     # compile src/main.cr into ./<name> using core.toml
core run       # build, then run the binary (passes through extra args)
core test      # compile + run each tests/*.cr file; report pass/fail
core check     # type-check everything without generating code
```

Try them:

```bash
$ core run
built: demo
Hello, Core!
```

## A real program

Replace `src/main.cr`:

```core
// src/main.cr - the demo program
const PI: f64 = 3.14159265358979

func area(r: f64) -> f64 {
    return PI * r * r
}

func main() {
    for i in 1..=5 {
        r = i as f64
        say "radius " + to_string(i) + " -> area " + to_string(area(r))
    }
}
```

Run it:

```bash
$ core run
built: demo
radius 1 -> area 3.14159
radius 2 -> area 12.56637
...
```

Add a test in `tests/test_area.cr`:

```core
// tests/test_area.cr
const PI: f64 = 3.14159265358979

func area(r: f64) -> f64 {
    return PI * r * r
}

func test_area() {
    assert(area(1.0) > 3.14, "unit circle area")
    assert(area(2.0) > 12.56, "radius two area")
}

func main() {
    test_area()
    say "tests ok"
}
```

(Here the test file redefines `area` — test files are separate programs; see the v0.1 testing notes below.)

```bash
$ core test
built: core_test_test_area
tests ok
---- running tests/test_area.cr

1/1 test files passed
```

Notes on testing in v0.1: each `tests/*.cr` file is compiled **as its own entry point** — it needs its own `func main()` that calls its `test_*` functions and uses `assert` from the prelude to fail loudly.

## Standalone files (no project)

Single-file programs skip the manifest entirely:

```bash
core compile mycoolbinary main.cr   # produces ./mycoolbinary
./mycoolbinary
```

The first argument is the exact output binary name, the second is the entry `.cr` file. This is what `core build` does under the hood, with `src/main.cr` and the manifest's settings.

## How compilation works

Every imported `.cr` file is compiled into the **same final executable** (whole-program compilation). Binaries are self-contained: you can delete the sources and the binary still runs. There is no dynamic Core runtime to distribute — only libc and the thin `corert.o` routines, linked statically into your program.

## Common mistakes

- **Running `core build` outside a project.** You'll get `no core.toml in this directory`. Use `core compile <out> <file.cr>` for standalone files.
- **Missing `func main()`.** `core build` looks for `src/main.cr` (per `source-dir`) and requires a `main`.
- **Expecting `core test` to find tests without a `main`.** Each test file runs as a program; give it `func main()` calling its `test_*` functions.
- **Editing `core.lock` by hand.** It's managed by `core install/remove/update`; see [packages.md](packages.md).

## Performance notes

- Default is `-O0`. For real builds use `core compile` with `-O2`/`-O3`/`-Os` — the LLVM pipeline makes an enormous difference.
- `--debug` adds DWARF info so GDB breakpoints resolve to your `.cr` lines; combine with `-O0` while debugging.

## Exercises

1. Scaffold a project, rename it in `core.toml`, and confirm `core build` produces the new binary name.
2. Add a second module under `src/` and import it (see [modules.md](modules.md)).
3. Break the type checker on purpose (`say "Age: " + 42`) and run `core check` to see the error without codegen.

Next: [Variables](variables.md).
