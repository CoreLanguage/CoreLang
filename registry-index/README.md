# packages — public Core package submissions (SEPARATE repo)

This is **not** part of the main Core repo. It seeds a standalone
submissions repository, `corelanguage/packages`, where authors propose
packages and maintainers review them. The main compiler repo never
accepts package submissions directly.

Create it once (needs a GitHub login with org rights):

```sh
gh repo create corelanguage/packages --public --description \
  "Public Core package submissions for the corepkg registry"
cp -r registry-index/* /tmp/packages-seed/ && cd /tmp/packages-seed
git init -b main && git add . && git commit -m "init package index"
git remote add origin git@github.com:corelanguage/packages.git
git push -u origin main
```

(Without `gh`: create the empty repo in the GitHub web UI, then run the
`git init … git push` lines above.)

## How submissions work

1. Author opens a PR adding `packages/<name>/meta.toml`:

```toml
[package]
name = "coolmath"
version = "1.0.0"
description = "tiny math helpers"
source = "https://github.com/someone/coolmath"  # audited source repo + tag
tag = "v1.0.0"
sha256 = "<tarball sha256, filled in by maintainer>"
```

2. CI runs `scripts/validate.py` (name/version/dup checks).
3. Maintainer reviews, then publishes to the VPS registry:

```sh
git clone <source> --branch <tag> /tmp/coolmath
COREPKG_REGISTRY=https://pkg.core-lang.example \
  COREPKG_TOKEN=$REGISTRY_TOKEN tools/corepkg/corepkg publish /tmp/coolmath
```

Users never touch GitHub: `corepkg install coolmath@^1.0` hits only the VPS.
