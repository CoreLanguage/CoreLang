# packages — public Core package submissions

This directory is the **template** for the public submissions repo
(`corelanguage/packages`). I cannot create the GitHub org repo for you;
create it, then copy this directory in as the repo root:

```sh
# you (org owner), once:
gh repo create corelanguage/packages --public
cp -r registry-index/* /tmp/packages/ && cd /tmp/packages
git add . && git commit -m "init package index" && git push -u origin main
```

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
