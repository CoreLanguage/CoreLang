# Hosted apt repository (VPS)

Users install with:

```sh
curl -fsSL http://94.24.39.227/key.gpg | sudo gpg --dearmor -o /usr/share/keyrings/core.gpg
echo "deb [signed-by=/usr/share/keyrings/core.gpg] http://94.24.39.227 stable main" \
  | sudo tee /etc/apt/sources.list.d/core.list
sudo apt update && sudo apt install core
```

(Bare `apt install core` without the lines above cannot work: Debian has
no knowledge of this repo until it is added. Same applies to the yum/dnf
rpm, apk and AUR artifacts in this directory — each needs its repo
configured first; see packaging/README.md.)

## Server layout (94.24.39.227)

- `/var/www/core-apt/` — repo root, served by nginx on port 80
  - `pool/main/core_<ver>_amd64.deb`
  - `dists/stable/main/binary-amd64/{Packages,Packages.gz}`
  - `dists/stable/{Release,InRelease,Release.gpg}`
  - `key.gpg` — ASCII-armored GPG public key (`packages@core-lang.example`)
- Release metadata: `/tmp/apt-release.conf` recipe (Origin/Core, stable/main)
- Signing key: root's GPG keyring (`GNUPGHOME=/root/.gnupg`), no passphrase

## Publishing a new version

```sh
VERSION=0.2.0
# 1. build the tarball, then the deb (from any checkout of this repo):
./scripts/build-release.sh
./packaging/build-deb.sh $VERSION dist/core-linux-x86_64.tar.gz /tmp/debs
scp /tmp/debs/*.deb root@94.24.39.227:/var/www/core-apt/pool/main/

# 2. on the VPS, re-index and re-sign:
ssh root@94.24.39.227 "cd /var/www/core-apt && \
  apt-ftparchive packages pool/main > dists/stable/main/binary-amd64/Packages && \
  gzip -kf dists/stable/main/binary-amd64/Packages && \
  apt-ftparchive -c /tmp/apt-release.conf release dists/stable > dists/stable/Release && \
  gpg --batch --yes --clearsign -o dists/stable/InRelease dists/stable/Release && \
  gpg --batch --yes -abs -o dists/stable/Release.gpg dists/stable/Release"

# 3. users upgrade with: sudo apt update && sudo apt upgrade core
```
