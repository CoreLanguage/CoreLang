# Modules

Every `.cr` file is a module. Imports pull other files into your program (whole-program compilation — all imported files end up in the same binary), and `pub` controls what's visible across the boundary.

## Importing

```core
import math                      // import a module
import utils.helper              // import utils/helper.cr, binds it as `helper`
import utils.helper as util      // rename the binding
```

- Imports are **file-level** (top of the file).
- `import math` binds the name `math`; members are reached with a dot: `math.sqrt(2.0)`.
- Sub-Paths (`import utils.helper`) resolve to files relative to the importer.
- The **prelude** (`std/prelude.cr`) is auto-imported — that's why `say`, `assert`, `panic`, `Option`, `Result`, `to_string` need no import.

## `pub`: the visibility boundary

Declarations without `pub` are **private to their module**:

```core
// utils/helper.cr
func helper_impl() -> i32 { return 7 }      // private
pub func twice(x: i32) -> i32 { return x * 2 }   // exported

pub struct Vec2 { x: f64, y: f64 }          // exported type
pub class Holder { ... }                    // exported class
```

Trying to use `helper_impl` from another module fails: `unknown function 'helper_impl'`. Non-pub module members (consts, globals) fail with `module 'x' has no public member 'y'`.

> **v0.1 note:** visibility of *members inside* classes/structs (private by default for classes) is currently advisory — the enforced boundary is at module level. See [classes.md](classes.md).

## Resolution order

When you `import utils.helper`, the compiler searches, in order:

1. the **directory of the importing file** (and its subdirectories, matching the dotted path)
2. the project's **`src/`** directory (and `source-dir` from `core.toml`)
3. **installed dependency packages** (from the package cache / `core.lock` — see [packages.md](packages.md))
4. the **standard library** (`std/` next to the compiler)

First hit wins. This lets projects shadow stdlib names deliberately — with the usual footgun consequences.

## A multi-module project

```
myapp/
├── core.toml
└── src/
    ├── main.cr        // entry point
    ├── geometry.cr
    └── algebra.cr
```

```core
// src/algebra.cr
pub func sqrt_v(x: f64) -> f64 {
    mut guess = x
    for i in 0..24 { guess = (guess + x / guess) / 2.0 }
    return guess
}
```

```core
// src/geometry.cr
import algebra

pub struct Vec2 {
    x: f64
    y: f64
}

pub func dist(v: Vec2) -> f64 {
    return algebra.sqrt_v(v.x * v.x + v.y * v.y)
}
```

```core
// src/main.cr
import geometry

func main() {
    say geometry.dist(geometry.Vec2 { x: 3.0, y: 4.0 })   // 5.0
}
```

Build from the project root:

```bash
core compile geometry_demo src/main.cr
./geometry_demo    # 5
```

Chained imports work — `main` imports `geometry`, which imports `algebra`; all three compile into one binary. Re-importing the same module through two paths is fine (deduplicated).

## Circular imports are errors

`a.cr` importing `b` while `b.cr` imports `a` is rejected with a clear cycle trace:

```
error: circular import detected:
    -> src/main.cr
    -> src/a.cr
    -> src/b.cr
    -> src/a.cr
```

Restructure instead: move the shared declarations to a third module both import, or pass values as parameters instead of reaching back.

## Layout conventions

- **One module, one topic.** `geometry.cr` holds geometry; don't build a `utils.cr` junk drawer.
- **`main.cr` stays thin** — it wires modules together; logic lives in modules.
- **Dotted paths mirror directories**: `import utils.helper` → `src/utils/helper.cr`.
- **`pub` is your API surface.** Everything else is implementation detail, free to refactor.

## Complete working example

The three-file project above is complete — create those files, run `core compile geometry_demo src/main.cr`, and it prints `5`. The `import ... as` form:

```core
import math as m

func main() {
    say m.sqrt(25.0)     // 5
}
```

## Common mistakes

- **Missing `pub`** on declarations other modules need — the error is `unknown function/type/member`, which reads like a typo. Check the export first.
- **Importing from the wrong root.** Paths resolve relative to the *importing file* (then src/, packages, std) — `import utils.helper` from `src/geometry.cr` looks next to `geometry.cr` first.
- **Circular imports.** Not fixable with casts — restructure (see above).
- **Cross-module `const`.** Reading a non-`pub` const fails; and even `pub const` cross-module reads are unreliable in v0.1 — prefer `pub func` accessors or `pub` globals.
- **Shadowing stdlib names.** A local `math.cr` shadows the stdlib `math` for files that resolve it first — name modules uniquely.

## Performance notes

- Modules are a *source* concept: everything compiles into one executable; calls between modules are ordinary direct calls, inlined at `-O2` like any other function.
- No link-time cost per import — unused `pub` functions are dead-stripped.
- Whole-program compilation means full cross-module optimization, but no incremental builds: large projects recompile everything on each build.

## When to use / not use

- **Split modules** when a file passes ~300–500 lines or two concerns stop changing together.
- **Don't** create one-file-per-function layouts; the unit of reuse is the module, not the function.
- **Don't** use imports where parameters would do — a function taking `Vec2` shouldn't import your geometry internals.
- For distributing reusable libraries to other projects, that's packages — see [packages.md](packages.md).

Next: [Projects](projects.md).
