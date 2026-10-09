# Registry packages (corepkg, no GitHub required)

The default `core install` flow is git-based (docs/packages/overview.md):
the package address *is* a git URL. `corepkg` adds a registry flow for
users who should never touch git hosting:

```sh
export COREPKG_REGISTRY=http://94.24.39.227:8080   # your VPS
corepkg search math
corepkg install coolmath@^1.0     # vendors ./core_packages/coolmath
core build                        # path dep, works offline from GitHub
```

- Registry server: `registry/server.py` (+ `registry/core-registry.service`)
- Submissions index template: `registry-index/` (copy into `corelanguage/packages`)
- Lock: `core.lock.json` pins `{version, sha256, registry}` per package
- Publish (maintainer): `corepkg publish <dir>` with `COREPKG_TOKEN`

Tarballs are sha256-verified against the index on every install.
