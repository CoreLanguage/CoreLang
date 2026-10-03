# Packages — Overview

Core has a **git-based package ecosystem with no central registry**. A
package is a git repository containing a `core.toml` manifest and Core
sources. The `core` tool clones, versions, and links them directly.

Related: [manifest.md](manifest.md), [lockfile.md](lockfile.md),
[publishing.md](publishing.md), [../stdlib/overview.md](../stdlib/overview.md).

---

## 1. There is no registry

- There is no npm-style index, no search, no namesquatting, and no
  moderation layer. The "address" of a package **is** its repository:
  - `github.com/user/repo` — shorthand for `https://github.com/user/repo.git`
  - any git URL (`https://…`, or a local path `/abs/path` or `./rel`)
- Versioning is repository **git tags** named `vX.Y.Z` (semver).
- Discovery happens however you like (README links, word of mouth);
  the tool never contacts anything except git remotes.

## 2. Installing and using

```console
core install github.com/snitch/coolmath        # remote
core install github.com/snitch/coolmath@^1.2   # with a version constraint
core install /tmp/mockrepo/coolmath            # local git repo (dev)
core list                                      # requirements + resolved
core update                                    # re-resolve to newest
core remove coolmath
```

`install` writes the requirement into `[dependencies]` of `core.toml`
and pins the exact version + commit into `core.lock`; `build`/`run`/
`test`/`check` then fetch (cached in `~/.cache/core/git`) and put every
dependency's checkout directory on the import search path.

## 3. How imports resolve dependencies

`import coolmath` searches, in order (SPEC.md §9):

1. the importing file's directory,
2. the project's `src/` directory,
3. **each dependency checkout directory, in manifest order** — this is
   how a dependency's modules become importable,
4. the standard library.

So a package's *file names* are its *module names* (`coolmath.cr` is
module `coolmath`; `src/vec.cr` inside the dependency is importable as
`vec` — a dependency's internal `src/` layout is **not** prefixed).

Dependencies of dependencies (one level) are fetched and put on the
path the same way; each also contributes its `[build]` link settings.

## 4. Names and collisions

- **Module names come from file stems.** Two dependencies that both
  ship `utils.cr` both provide a module named `utils`; the first
  directory on the search path (manifest order) wins. There is no
  version-aware namespacing in v1.
- Avoid collisions by naming files distinctively (`coolmath.clamp.cr`
  or `clamp.cr` inside a `coolmath`-only repo).
- **Import bindings are per-file and aliasable.** If two needed modules
  share a stem, import with an alias:

```core
import json.utils            // binds `utils`
import myco.utils as mutils  // binds `mutils`
```

- **Symbols are mangled per defining module** (abi.md §8): two
  dependencies can both define `pub func parse(...)` without clashing
  — call them as `json.parse(...)` / `myco.parse(...)`. Ambiguous bare
  names are only an error if both are visible without qualification and
  called bare (overload resolution reports the ambiguity).
- **Enum variant ambiguity**: a bare variant name (`Some`) that exists
  in two visible enums is an error — qualify it (`Option<i32>.Some`).

## 5. The cache

- Checkouts live under `$XDG_CACHE_HOME/core` (default
  `~/.cache/core`): `git/` holds bare clones, `checkouts/<safe-name>/<version>/`
  holds exported worktrees used as import roots.
- Clones are fetched once and updated with `git fetch --all`;
  `core update` re-picks the best matching tag.
- The cache is safe to delete; the next build re-fetches.

## 6. Contracts between a project and its dependencies

- A dependency's `pub` declarations are the only ones you can use.
- A dependency's `[build]` link/link-paths/link-args are merged into
  the final link of your binary (publishing.md §5) — native libraries
  come from the dependency graph automatically.
- The dependency's own `core.lock` is ignored: **your** project's
  lockfile is the single source of truth; transitive requirements are
  re-resolved from the dependency manifests at build time.
