#!/usr/bin/env python3
"""Core package registry - stdlib-only HTTP server.

Run on the VPS:
    REGISTRY_DATA=/var/lib/core-registry REGISTRY_TOKEN=<secret> python3 server.py 8080

Storage layout under $REGISTRY_DATA:
    index.json
    <name>/<version>.tar.gz
    <name>/<version>.meta.json
"""
import base64
import hashlib
import json
import os
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, unquote

DATA = os.environ.get("REGISTRY_DATA", os.path.join(os.path.dirname(__file__), "data"))
TOKEN = os.environ.get("REGISTRY_TOKEN", "")
NAME_RE = re.compile(r"^[a-z0-9][a-z0-9_-]{1,63}$")
VER_RE = re.compile(r"^\d+\.\d+\.\d+$")


def load_index():
    path = os.path.join(DATA, "index.json")
    if not os.path.exists(path):
        return {"packages": {}}
    with open(path) as f:
        return json.load(f)


def save_index(idx):
    os.makedirs(DATA, exist_ok=True)
    with open(os.path.join(DATA, "index.json"), "w") as f:
        json.dump(idx, f, indent=2)


class Handler(BaseHTTPRequestHandler):
    server_version = "CoreRegistry/0.1"
    timeout = 10  # drop slowloris-style half-open connections

    def log_message(self, *a):
        sys.stderr.write("%s - %s\n" % (self.address_string(), a[0] % a[1:]))

    def _json(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urlparse(self.path)
        parts = [unquote(p) for p in u.path.strip("/").split("/") if p]
        if parts[:2] == ["api", "packages"] and len(parts) == 2:
            idx = load_index()
            out = []
            for name, vers in sorted(idx.get("packages", {}).items()):
                latest = sorted(vers.keys())[-1] if vers else None
                if latest:
                    m = vers[latest]
                    out.append({"name": name, "version": latest,
                                "description": m.get("description", ""),
                                "sha256": m.get("sha256", "")})
            return self._json({"packages": out})
        if parts[:2] == ["api", "packages"] and len(parts) == 3:
            idx = load_index()
            vers = idx.get("packages", {}).get(parts[2])
            if not vers:
                return self._json({"error": "not found"}, 404)
            return self._json({"name": parts[2], "versions": vers})
        if parts[:1] == ["download"] and len(parts) == 4:
            _, name, version, fname = parts
            fpath = os.path.join(DATA, name, fname)
            if not os.path.exists(fpath) or fname != f"{version}.tar.gz":
                return self._json({"error": "not found"}, 404)
            with open(fpath, "rb") as f:
                data = f.read()
            self.send_response(200)
            self.send_header("Content-Type", "application/gzip")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            return self.wfile.write(data)
        if parts == ["healthz"]:
            return self._json({"ok": True})
        return self._json({"error": "not found"}, 404)

    def do_POST(self):
        u = urlparse(self.path)
        if u.path != "/api/publish":
            return self._json({"error": "not found"}, 404)
        if TOKEN and self.headers.get("Authorization") != f"Bearer {TOKEN}":
            return self._json({"error": "unauthorized"}, 401)
        length = int(self.headers.get("Content-Length", 0))
        try:
            body = json.loads(self.rfile.read(length) or b"{}")
        except Exception:
            return self._json({"error": "invalid JSON"}, 400)
        name, version = body.get("name", ""), body.get("version", "")
        blob = body.get("tarball_b64", "")
        desc = body.get("description", "")
        if not NAME_RE.match(name):
            return self._json({"error": "invalid name"}, 400)
        if not VER_RE.match(version):
            return self._json({"error": "invalid version, want X.Y.Z"}, 400)
        try:
            raw = base64.b64decode(blob)
        except Exception:
            return self._json({"error": "invalid tarball_b64"}, 400)
        if len(raw) > 64 * 1024 * 1024:
            return self._json({"error": "tarball too large (64MB max)"}, 400)
        digest = hashlib.sha256(raw).hexdigest()
        pkgdir = os.path.join(DATA, name)
        os.makedirs(pkgdir, exist_ok=True)
        with open(os.path.join(pkgdir, f"{version}.tar.gz"), "wb") as f:
            f.write(raw)
        meta = {"name": name, "version": version, "description": desc,
                "sha256": digest, "size": len(raw)}
        with open(os.path.join(pkgdir, f"{version}.meta.json"), "w") as f:
            json.dump(meta, f, indent=2)
        idx = load_index()
        idx.setdefault("packages", {}).setdefault(name, {})[version] = meta
        save_index(idx)
        return self._json({"ok": True, "sha256": digest})


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    os.makedirs(DATA, exist_ok=True)
    print(f"core-registry serving {DATA} on :{port}", flush=True)
    srv = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    srv.daemon_threads = True
    srv.serve_forever()
