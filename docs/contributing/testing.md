# Testing the compiler

The compiler is tested end-to-end by `tests/run_tests.py`: it drives the
real `core` binary (compiles, runs, inspects output and diagnostics) the
way a user would. There are no unit tests — the e2e suite *is* the test
strategy, matching the compiler's size.

## Running it

```sh
python3 tests/run_tests.py --core build/core          # everything
python3 tests/run_tests.py --core build/core generics # one suite
ctest --test-dir build                                 # via CMake integration
```

Output is one line per check (`  ok <name>` / `  FAIL <name>` with up to
8 lines of detail) and a summary (`97 passed, 0 failed` at the time of
writing). Exit code is nonzero on any failure.

## Structure

```python
PASS = 0
FAIL = 0
FAILURES = []
CORE = "core"
```

Helpers (all module-level in `run_tests.py`):

| Helper | Purpose |
|---|---|
| `run(cmd, cwd, timeout)` | `subprocess.run` wrapper, captured output |
| `check(name, cond, detail)` | one assertion; appends to `PASS`/`FAILURES` |
| `compile_src(workdir, name, source, extra, expect_success)` | writes `<name>.cr`, runs `core compile <name>.bin <name>.cr [extra]`, returns `(result, binary, ok)` |
| `expect_output(workdir, source, expected_lines, extra)` | compiles, runs, compares stripped stdout lines and exit code 0 |
| `expect_compile_error(workdir, source, needle, name)` | asserts nonzero exit, `needle in stderr`, and **no binary produced** |
| `binary_of`, `unique_name` | naming helpers |

Each suite runs in its own `tempfile.TemporaryDirectory` (`w`), so tests
are hermetic and parallelizable.

The suites registered in `main()`:

| Suite | Covers |
|---|---|
| `basics` | literals, arithmetic/precedence, control flow, loops, switch |
| `functions` | calls, recursion, default arguments, overloading, lambdas, `never` |
| `types` | integer widths, casts, `sizeof`, string ops, arrays |
| `pointers-memory` | `ptr`, `&`/`*`, `alloc`/`free`, pointer arithmetic |
| `structs-classes` | structs, classes, inheritance, virtual dispatch, interfaces, init |
| `generics-enums` | generic functions/structs, monomorphization, enums, `match`, `Option` |
| `modules` | imports, module functions, dedup, binary independence, cycle errors |
| `core-init` | `core init` scaffolding, refusal to overwrite |
| `project-commands` | `build`/`run`/`check`/`test` |
| `packages` | install/list/update/remove against a local git repo |
| `codegen-internals` | `emit-ir`/`emit-asm`, object emission, GDB debug info, all `-O` levels |
| `cross-freestanding` | `--target=aarch64` object emission, freestanding kernel with inline asm |
| `unsafe-simd-ffi` | `unsafe`, SIMD builtins, C interop incl. varargs |
| `diagnostics` | negative tests: exact error-message needles |

## Adding a test

Pick (or create) the suite covering the feature. Two shapes:

**Behavior** — compile, run, compare output:

```python
def test_myfeature(w):
    expect_output(w, '''func main() {
    say mything(3)
}
''', ["6"])
```

**Rejection** — must fail to compile with a specific message:

```python
    expect_compile_error(w, 'func main() {\n    say mything()\n}\n',
                         "expects one argument")
```

Register new suite functions in the `tests = [...]` list in `main()`.

Guidelines:

- Write minimal programs — one behavior per `expect_output`.
- Assert *exact* output lines (`["9"]`), not substrings.
- Negative tests assert a stable needle from the diagnostic (see
  [../compiler/diagnostics.md](../compiler/diagnostics.md)) and rely on the
  helper's no-binary check to catch "compiled anyway" bugs.
- Multi-file features (modules, packages) create files under `w` with
  `os.makedirs`/`open(...).write` — see `test_modules` for the layout and
  `test_packages` for building a local git repo with semver tags.
- Never print to stdout in a test; put context in the `detail` argument of
  `check`.

## What to test (checklist per change type)

- **New syntax**: positive run, parse-error negative, and one test where
  the new syntax sits at a line boundary (newlines are significant).
- **New type**: positive arithmetic, `sizeof` via `say sizeof(T)`, cast
  round-trip, negative mixed-width test.
- **New builtin**: positive value check, negative arity/type check, and a
  shadowing test if users should be able to override it.
- **Diagnostics change**: update the needle in the existing
  `test_diagnostics` entry *in the same commit* as the message change.
- **Optimization changes**: the loop over `-O0..-Os` in
  `test_codegen_internals` must keep passing; add an `emit-ir -O2` needle
  if you want to pin a lowering.
- **Runtime (`corert.c`) changes**: every hosted test exercises it;
  freestanding tests guarantee the runtime stays optional.
- **Debug info**: keep the GDB batch check green
  (`--debug produces GDB-usable binaries`).

## Keeping the suite fast

Each check spawns several processes (compile + run + sometimes gdb); the
whole suite takes on the order of a minute. Prefer adding checks to
existing suites over new suites (each new suite adds tempdir setup), and
avoid `sleep`-based synchronization — `cmdRun` waits on process exit.
