# Packages

Packages are **git repositories** containing a Core project (a `core.toml` + module files). The tool clones them, resolves semver tags, and pins exact versions in `core.lock`.

## Adding a dependency

```bash
core install github.com/snitch/coolmath@^1.2.0
core install /path/to/local/package        # local path (dev dependencies)
core install https://gitlab.com/x/y@2.0.0  # full URL
```

`install` clones the repo, picks the best tag matching the constraint, records the dependency in `core.toml`, and pins the exact version + commit in `core.lock`:

```toml
[dependencies]
coolmath = "github.com/snitch/coolmath@^1.2.0"
```

```toml
[[packages]]
name = "coolmath"
version = "1.4.2"
source = "github.com/snitch/coolmath"
commit = "5a72d84c268248a1ce87beca57cef43e2ecea1cc"
sha256 = ""
dependencies = []
```

Then import and use it like any module:

```core
import coolmath

func main() {
    say coolmath.lerp(0.0, 10.0, 0.25)   // 2.5
}
```

## Constraints

The version goes after `@` in the requirement:

| Spec | Meaning |
|---|---|
| `@^1.2.3` | caret: `>=1.2.3` and `<2.0.0` (for 0.x: `^0.2.3` allows only `0.2.x`; `^0.0.3` only `0.0.3+`) |
| `@>=1.2.0` | floor: at least this version |
| `@1.2.3` | exact version |
| `@` / omitted | any version — the newest tag wins |

Resolution picks the **highest tag** satisfying the constraint. Tags are matched by stripping a leading `v` (`v2.1.0` = `2.1.0`).

## Local path dependencies

`core install ./sibling-pkg` (or an absolute path) registers the dependency **by path** — no cloning; edits in the dependency are visible on the next build. Use it while developing two packages together; CI/other consumers should use git specs.

## The cache

Remote packages clone into `~/.cache/core/git/<sanitized-url>.git` (or `$XDG_CACHE_HOME/core/git`). Re-installing fetches (`git fetch --all`) and checks out the resolved tag. Local paths are referenced in place — never copied.

## The other commands

```bash
core list        # show name, requirement, resolved version + commit
core update      # re-resolve every dependency to the newest matching version
core remove name # drop the dependency from core.toml and core.lock
```

`update` respects constraints: `^1.2.0` moves 1.2.0 → 1.9.9, never to 2.x. Edit the constraint in `core.toml` first if you want a major bump.

## What a package looks like

```
coolmath/
├── core.toml           # name + version (tags drive resolution)
└── coolmath.cr         # module named after the package; pub declarations
```

```toml
# coolmath/core.toml
[package]
name = "coolmath"
version = "1.2.0"
```

```core
// coolmath/coolmath.cr
pub func lerp(a: f64, b: f64, t: f64) -> f64 {
    return a + (b - a) * t
}
```

Tag releases to make them installable: `git tag v1.2.0 && git push --tags`.

## Complete working example

Verified against a local mock repo:

```bash
# one-time: create a package
mkdir -p /tmp/coolstrings && cd /tmp/coolstrings && git init -q
printf '[package]\nname = "coolstrings"\nversion = "2.1.0"\n' > core.toml
printf 'pub func shout(s: string) -> string { return s + "!" }\n' > coolstrings.cr
git add core.toml coolstrings.cr && git commit -qm "1.0" && git tag v2.1.0

# consume it
mkdir -p /tmp/app && cd /tmp/app && core init app
printf 'import coolstrings\n\nfunc main() {\n    say coolstrings.shout("packaged")\n}\n' > src/main.cr
core install /tmp/coolstrings
core build && ./app        # prints: packaged!
core list                  # shows resolved 2.1.0 + commit
```

## Common mistakes

- **No git tags in the package repo.** Resolution reads tags; an untagged repo yields no versions (the tool reports it resolved nothing).
- **Editing `core.lock` by hand.** Regenerate with `install/update/remove`.
- **Forgetting the import.** `core install` only registers the dependency — your code still needs `import coolstrings`.
- **Package module name mismatch.** The import name is the repo's *file* name (`coolstrings.cr` → `import coolstrings`), not the manifest name.
- **Private repos.** Cloning uses your `git` credentials/ssh config; make sure `git clone <url>` works interactively first.
- **Deleting the cache while pins exist** — `core build` re-fetches, but offline builds fail.

## Performance notes

- Dependencies compile into your binary (whole-program compilation) — no dynamic loading, no version conflicts at runtime.
- Unused `pub` functions from dependencies are dead-code-eliminated at `-O2`.
- `core build` re-resolves dependencies each run; local path deps make the edit-compile loop instant.

## When to use / not use

- **Use packages** once code is shared across two-plus projects or needs standalone versioning.
- **Don't** publish trivial single functions — copy them; the maintenance tail isn't worth it.
- **Don't** depend on an unreleased branch; ask the maintainer to tag, or use a local path dep while iterating.
- Keep stdlib usage (`math`, `memory`, `thread`) import-direct — they're not packages.

## Exercises

1. Build the local mock-repo walkthrough from "Complete working example" end to end.
2. Change the constraint to `@^2.0.0`, bump your mock package's tag to `v2.2.0`, and run `core update` — confirm `core list` shows the new version.
3. Add a second function to the package, commit, tag `v2.2.1`, update, and use the new function from your app.
4. Switch the dependency to a **local path** (`core install ../coolstrings`) and verify edits to the package show up on the next build without reinstalling.
5. Run `core remove` and confirm both `core.toml` and `core.lock` are clean — then re-install and diff the two files.

Next: [Concurrency](concurrency.md).
