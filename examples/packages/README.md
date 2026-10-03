# Packages example

Demonstrates the git-based package manager. The dependency is a git
repository (local path or remote URL both work):

```console
# from this directory:
core install /tmp/mockrepo/coolmath     # local git repo
# or:
core install github.com/user/coolmath   # remote (clones https://github.com/user/coolmath.git)
core build
./packages_demo
```

## Making a local mock repository

```console
mkdir -p /tmp/mockrepo/coolmath && cd /tmp/mockrepo/coolmath
git init
cat > core.toml << 'TOML'
[package]
name = "coolmath"
version = "1.0.0"
TOML
cat > coolmath.cr << 'CORE'
pub func lerp(a: f64, b: f64, t: f64) -> f64 {
    return a + (b - a) * t
}
CORE
git add core.toml coolmath.cr
git commit -m "coolmath 1.0.0"
git tag v1.0.0
```

Then run `core install /tmp/mockrepo/coolmath` here. See
docs/packages/publishing.md for the full publishing workflow.
