# Modules and packages

## Modules

Every `.cr` file is a module named by its file stem. `src/utils/math.cr`
is module `math` (the stem, not the directory). A module's `pub`
declarations are what other modules can use; everything else is
private.

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
    say geometry.dist(geometry.Vec2 { x: 3.0, y: 4.0 })   // 5
}
```

## Imports

`import a.b` binds the name `b` (the last component); `as` renames it.
The imported name is a module namespace: `math.sqrt(...)`,
`geometry.Vec2`.

```core
import math
import utils.helper        // binds `helper`
import json.utils          // binds `utils`
import myco.utils as mutils  // alias when two modules share a stem
```

Resolution order for `import a.b`, first hit wins:

1. the directory of the importing file (`a/b.cr`)
2. the project `src/` directory
3. each dependency package's checkout directory, in manifest order
4. the standard library

Cyclic imports are errors. Importing the same file twice is deduplicated.

## Visibility

`pub` on any declaration (function, struct, field, method, global,
const) makes it visible to importing modules. Without `pub`, using it
from another module is a compile error. Symbols are mangled per
defining module, so two dependencies can both define `pub func parse`
without clashing, as long as you call them qualified.

The prelude is loaded before everything; its names (`say`, `Option`,
`assert`, `panic`, ...) are visible everywhere without an import, and
an explicit import shadows them.

## Projects: core.toml

`core init` writes a manifest:

```toml
[package]
name = "hello"
version = "0.1.0"
core-version = ">=0.1.0"
source-dir = "src"

[build]
link = []                # native libraries to link, e.g. ["m"]

[dependencies]
# coolmath = "github.com/snitch/coolmath@^1.2.0"
```

`core build` compiles `src/main.cr` plus every imported module into one
executable named after `[package] name`. `core.lock` pins the exact
dependency versions.

## Packages

There is no central registry. A package is a git repository with a
`core.toml`; its address is its repository:

```console
core install github.com/snitch/coolmath          # remote
core install github.com/snitch/coolmath@^1.2     # version constraint
core install /tmp/mockrepo/coolmath              # local git repo, for development
core list                                        # requirements + resolved versions
core update                                      # re-resolve to newest compatible
core remove coolmath
```

`install` writes the requirement into `[dependencies]` and pins the
version + commit into `core.lock`. Builds fetch into `~/.cache/core/git`
and put each dependency's checkout on the import search path, so the
dependency's file names become module names (`vec.cr` is importable as
`vec`, not `coolmath.vec`).

Two consequences worth knowing:

- Module names come from file stems. Two dependencies both shipping
  `utils.cr` collide; the first on the search path wins. Name your files
  distinctively, and alias imports when needed (`import x as y`).
- Symbols are mangled per defining module, so two dependencies can both
  define `pub func parse(...)`; call them as `json.parse` /
  `myco.parse`.

A dependency's `[build]` link settings merge into your final link, so
native libraries come from the dependency graph automatically.

More detail: [packages/overview.md](../packages/overview.md),
[packages/manifest.md](../packages/manifest.md).
