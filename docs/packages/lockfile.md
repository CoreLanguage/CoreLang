# The `core.lock` Lockfile

`core.lock` sits next to `core.toml` and records the **exact resolved
state** of the dependency graph. It is written and read by the `core`
tool; humans normally do not edit it.

---

## 1. Format

```toml
# Core lockfile - records exact resolved dependency versions.
# Managed by the core toolchain; do not edit by hand while builds are in flight.

[[packages]]
name = "coolmath"
version = "1.4.2"
source = "github.com/snitch/coolmath"
commit = "8f3a2c1b9d4e..."
sha256 = ""
dependencies = []
```

It is TOML: one `[[packages]]` array-of-tables entry per resolved
dependency, in the order they were resolved (manifest order; `update`
rewrites the whole list in manifest order).

### Fields

| Key | Meaning |
|-----|---------|
| `name` | Dependency key — matches the `[dependencies]` key in `core.toml`. |
| `version` | Resolved semver (the tag with the leading `v` stripped). A repo with no version tags resolves to `"0.0.0+" + first 7 chars of HEAD commit`. |
| `source` | The repository spec as written (e.g. `github.com/snitch/coolmath`, or a local path). |
| `commit` | Git commit the version was resolved from (full hash). For local path deps: HEAD of the dep's repo, or the literal `local` when it is not a git repo. |
| `sha256` | Content hash — **currently always empty**; the git commit pins content. Kept in the format for forward compatibility. |
| `dependencies` | Names of the entry's transitive dependencies (for tooling/display). |

An empty project's lockfile is just the header plus a comment line:

```toml
# Core lockfile - records exact resolved dependency versions.
# Managed by the core toolchain; do not edit by hand while builds are in flight.
# No dependencies yet. Packages installed with `core install` appear here.
```

## 2. Who writes it, and when

| Command | Effect on core.lock |
|---------|---------------------|
| `core install <pkg>` | Adds or replaces the entry for `<pkg>` (all other entries preserved). |
| `core remove <name>` | Drops the entry. |
| `core update` | Re-resolves **every** dependency to the newest version matching its constraint and rewrites the lockfile with exactly those entries. |
| `core init` | Creates an empty lockfile. |
| `core build` / `run` / `test` / `check` | **Read-only.** Builds **respect the pins**: a dependency present in `core.lock` is fetched at exactly the pinned version/commit; only dependencies missing from the lockfile resolve fresh from the manifest (a missing lockfile is treated as empty). |

## 3. Reproducibility model

- The lockfile pins `commit` per dependency, so rebuilding the same
  commit of your project with the same `core.lock` fetches the same
  dependency content — **provided the tags still exist and the commits
  are reachable**. Git commits are immutable; tags can be deleted or
  moved, so keep the lockfile in version control.
- `sha256` is not populated in v1, so content-addressed verification is
  not available yet — the commit hash is the pin.
- Builds prefer the lockfile pins over re-resolution: delete an entry
  (or the whole file) to force a fresh resolution from the manifest's
  constraints, and run `core update` to re-lock everything at the newest
  matching versions.
- Transitive dependencies are re-resolved from each dependency's
  manifest at build time; only your direct dependencies appear in your
  lockfile.

## 4. Should I commit it?

Yes. Committing `core.lock` makes every clone resolve the same
dependency commits, which is the closest thing to reproducible builds
the git-based ecosystem offers today.
