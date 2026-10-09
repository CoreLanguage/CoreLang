# Core package registry (self-hosted, no GitHub required)

The registry is a tiny HTTP service you run on your VPS. `corepkg`
downloads versioned tarballs from it. Package *metadata* lives in a
public submissions repo (`corelanguage/packages`); the *bits* live on
your VPS.

```
author  ->  submits meta.toml to corelanguage/packages (PR)
maintainer -> reviews, builds tarball, uploads to VPS registry
user    ->  corepkg install <name>   (talks only to the VPS)
```

## Layout

```
registry/
  server.py        stdlib-only HTTP registry (no pip deps)
  packages.example/  example on-disk storage layout
  core-registry.service  systemd unit for the VPS
  README.md        VPS deploy instructions
```

## API

| Method | Path | Description |
|---|---|---|
| `GET` | `/api/packages` | JSON list `[{name, version, description, sha256}]` (latest per name) |
| `GET` | `/api/packages/<name>` | All versions of one package |
| `GET` | `/download/<name>/<version>/<file>` | Download the tarball |
| `POST` | `/api/publish` | Upload (requires `Authorization: Bearer $REGISTRY_TOKEN`), multipart or JSON+base64 |

Storage on disk:

```
$REGISTRY_DATA/
  index.json
  <name>/<version>.tar.gz
  <name>/<version>.meta.json   {name, version, description, sha256, ...}
```

## Running

```sh
REGISTRY_DATA=/var/lib/core-registry REGISTRY_TOKEN=secret python3 registry/server.py 8080
```
