#!/usr/bin/env python3
"""Validate registry-index submissions: python3 scripts/validate.py packages/"""
import os
import re
import sys

NAME_RE = re.compile(r"^[a-z0-9][a-z0-9_-]{1,63}$")
VER_RE = re.compile(r"^\d+\.\d+\.\d+$")
seen = {}
errors = 0
root = sys.argv[1] if len(sys.argv) > 1 else "packages"
for name in sorted(os.listdir(root)):
    meta = os.path.join(root, name, "meta.toml")
    if not os.path.exists(meta):
        print(f"error: {name}/ missing meta.toml")
        errors += 1
        continue
    text = open(meta).read()
    get = lambda k: (re.search(rf'(?m)^{k}\s*=\s*"([^"]+)"', text).group(1)
                     if re.search(rf'(?m)^{k}\s*=\s*"([^"]+)"', text) else "")
    n, v = get("name"), get("version")
    if n != name:
        print(f"error: {meta}: dir/name mismatch ({name} vs {n})")
        errors += 1
    if not NAME_RE.match(n):
        print(f"error: {meta}: bad name")
        errors += 1
    if not VER_RE.match(v):
        print(f"error: {meta}: bad version (want X.Y.Z)")
        errors += 1
    if (n, v) in seen:
        print(f"error: duplicate {n} {v}")
        errors += 1
    seen[(n, v)] = True
    for k in ("description", "source", "tag"):
        if not get(k):
            print(f"error: {meta}: missing {k}")
            errors += 1
print("ok" if not errors else f"{errors} error(s)")
sys.exit(1 if errors else 0)
