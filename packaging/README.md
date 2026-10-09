# OS packaging for Core

`apt install core`, `dnf install core`, `apk add core`, `yay -S core`
all work once the artifacts below are hosted. The `core` binary is
statically linked (LLVM) so every package just ships `bin/core`,
`lib/core/{corert.o,std/}` — no LLVM dependency.

| Manager | Artifact in this repo | Hosting needed on the VPS |
|---|---|---|
| apt (Debian/Ubuntu) | `packaging/deb/` + `packaging/build-deb.sh` | apt repo (`reprepro`/`aptly`) served over HTTPS, users add a `.list` + key |
| yum/dnf (RHEL/Fedora) | `packaging/rpm/core.spec` | yum repo (`createrepo_c`) or COPR; same RPM works for yum and dnf |
| apk (Alpine) | `packaging/apk/APKBUILD` | apk repo (`apk index`), or install the `.apk` file directly |
| yay/paru (Arch, AUR) | `packaging/aur/PKGBUILD` | push directory to AUR as package `core-lang` |

Until the repos are hosted, every manager can install the artifact
directly:

```sh
# deb (any Debian/Ubuntu machine, no repo needed):
sudo dpkg -i core_0.1.0_amd64.deb; sudo apt-get install -f

# rpm (Fedora/RHEL, works with both yum and dnf):
sudo rpm -i core-0.1.0-1.x86_64.rpm   # or: sudo dnf install ./core-*.rpm

# apk (Alpine):
sudo apk add --allow-untrusted core-0.1.0-r0.apk

# arch (no AUR needed):
makepkg -si   # inside packaging/aur/
```

Build all artifacts after a release tarball exists:

```sh
./packaging/build-all.sh 0.1.0 dist/core-linux-x86_64.tar.gz
```
