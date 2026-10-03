# Authoring and Publishing Core Packages

A Core package is a **git repository**. Publishing is making the
repository reachable and tagging a version. There is no registry, no
upload step, and no account — the URL is the identity
([overview.md](overview.md)).

---

## 1. Repository layout

```text
coolmath/                 ← repo root
├── core.toml             ← required manifest
├── core.lock             ← optional; usually not needed for libraries
├── coolmath.cr           ← modules = file names (stems)
├── src/                  ← optional; put modules here if you like
│   └── vec.cr
├── tests/                ← `core test` compiles each *.cr as an entry
│   └── main.cr
├── examples/
│   └── demo/main.cr
├── vendor/               ← optional: native .so/.a files + headers
│   └── lib/libfast.a
└── README.md
```

Rules:

- `core.toml` at the root, with an accurate `[package] name` and
  `version`.
- Every `.cr` file is a module named by its stem; only `pub`
  declarations are usable by consumers.
- `core build` in a library repo is only meaningful if it has
  `src/main.cr`; libraries are fine without one (consumers import the
  module files directly).

Scaffolding locally:

```console
mkdir coolmath && cd coolmath
core init coolmath          # refuses to overwrite an existing core.toml
# edit core.toml, write coolmath.cr, git init + commit
```

## 2. A minimal package

`core.toml`:

```toml
[package]
name = "coolmath"
version = "1.0.0"
core-version = ">=0.1.0"

[build]
link = []
```

`coolmath.cr`:

```core
pub func lerp(a: f64, b: f64, t: f64) -> f64 {
    return a + (b - a) * t
}

pub const TAU: f64 = 6.28318530717958647692
```

(The package example in `examples/packages` does exactly this against a
local mock repo.)

## 3. Releases are git tags

```console
git tag v1.0.0
git push origin v1.0.0
```

- Tag names are semver, optionally `v`-prefixed: `1.2.3` or `v1.2.3`.
  Tags without a valid semver tail are ignored by resolution.
- Make the tag's version match `[package] version` (the manifest
  version is what `core install` reports for local path deps; the tag
  is what remote consumers get).
- Every release = one tag. No branch naming requirements; `main` is
  irrelevant except as the HEAD fallback (§4).

## 4. Version constraints and resolution

Consumers constrain; resolution picks the **highest matching tag**:

| Constraint | Matches |
|------------|---------|
| `^1.2.3` (major > 0) | same major, `>= 1.2.3` — so `^1.2.3` accepts 1.2.3, 1.9.0; not 2.0.0 |
| `^0.2.3` (major = 0, minor > 0) | `0.2.x`, `>= 0.2.3` — not 0.3.0 |
| `^0.0.3` (major = minor = 0) | `0.0.3` and later patches only |
| `>=1.0.0` | any version ≥ 1.0.0 (any major) |
| `1.2.3` (bare/exact) | exactly 1.2.3 |
| *(no constraint)* | any version; if the repo has **no** version tags at all, HEAD of the default branch (recorded as version `0.0.0+<sha7>`) |

Semantics to know:

- Comparison is numeric over `major.minor.patch`; build metadata and
  pre-release suffixes are not parsed (a tag `v1.2.3-rc1` parses as
  `1.2.3` — avoid them).
- Ties (two tags with the same version) resolve by whichever comes last
  in `git tag --list` order — don't create duplicate version tags.
- `core update` re-resolves all constraints to the newest matching
  versions and rewrites `core.lock`.
- There is no global solver: each dependency is resolved independently
  against its own constraint. Two deps may use different major versions
  of a shared package only if they have different module names — same
  module name from two checkouts means the first manifest-ordered one
  wins (overview.md §4).

## 5. Native libraries in dependencies

If your package wraps or ships native code, declare it in `[build]` and
every consumer's final link picks it up automatically:

```toml
[build]
link = ["fastmath"]                # -> -lfastmath
link-paths = ["vendor/lib"]        # -> -Lvendor/lib  (relative to the checkout)
link-args = ["-Wl,-rpath,$ORIGIN/../vendor/lib"]
```

- Paths are used as written; make them relative to your repo root and
  commit the binaries (or a build script consumers run before
  `core build` — the tool does not run build scripts itself).
- Settings merge in dependency-manifest order before the consumer's own
  ([manifest.md](manifest.md) §3).
- The Core runtime (`-lm -lpthread -ldl -latomic`) is always linked
  first.

## 6. Local development with path dependencies

Work on a package and its consumer side by side:

```console
/home/me/dev/coolmath        # the library repo (git)
/home/me/dev/myapp           # the app, core.toml:
                             #   [dependencies]
                             #   coolmath = { path = "../coolmath" }
```

- A `path` dependency given as an **absolute path** (or a `./`-prefixed
  path) is used **in place**: edits take effect on the next `core
  build`, no reinstall, no copying (verified: adding a function to the
  dependency and rebuilding picks it up).
- A `../`-prefixed relative path goes through the normal git flow
  instead: the repo is cloned into the cache and the best matching tag
  is checked out (worktree) — tags are honored, but it is a snapshot,
  not in-place.
- `core install /abs/path/to/pkg` also records a path dependency (the
  manifest line keeps the path; the lockfile records the dep's current
  HEAD commit for reference).
- Before publishing, switch the manifest to the git form so consumers
  don't depend on your filesystem.
- A path dep is itself fetched-from-cache-free, but its **own**
  dependencies go through the normal git flow.

## 7. Checklists

### Publishing a package

- [ ] `core.toml` has the final `name` and `version`.
- [ ] Every public decl is marked `pub`; smoke-test with a fresh
      consumer: `core install <local-path> && core build`.
- [ ] `core test` passes (`tests/*.cr`, each with its own `func main()`
      that exits nonzero on failure via `assert`).
- [ ] Native libs committed or fetched by a documented script; `[build]`
      paths relative to the repo root.
- [ ] `README.md` states the import names (`import coolmath`) and
      license.
- [ ] Commit, tag `vX.Y.Z` matching `version`, push with the tag.

### Choosing constraints (as a consumer)

- [ ] Libraries: `^major.minor.patch` of the API you use.
- [ ] Applications: exact pins for release builds, `^` ranges during
      development, `core update` + review the `core.lock` diff.
- [ ] Never depend on the no-tag HEAD fallback for anything you care
      about — tag your deps.
- [ ] Commit `core.lock`: builds fetch the pinned commit exactly, so the
      lockfile is what makes other checkouts (and CI) reproducible.
