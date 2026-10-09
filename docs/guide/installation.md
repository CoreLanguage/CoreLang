# Installation

Core runs on 64-bit Linux: x86_64, aarch64, and riscv64. Three ways to
get the compiler: an apt repository, a release tarball, or building
from source.

## apt (Debian/Ubuntu)

One command:

```console
curl -fsSL http://94.24.39.227/setup-apt.sh | sudo bash
```

Or step by step:

```console
curl -fsSL http://94.24.39.227/key.gpg | sudo gpg --dearmor -o /usr/share/keyrings/core.gpg
echo "deb [signed-by=/usr/share/keyrings/core.gpg] http://94.24.39.227 stable main" \
  | sudo tee /etc/apt/sources.list.d/core.list
sudo apt update && sudo apt install core
```

Plain `apt install core` with nothing else only works after the repo
above is added — Debian has no knowledge of Core until then. Upgrades
come through the same repo: `sudo apt update && sudo apt upgrade core`.
Other managers are covered in [packaging/](../../packaging/): one RPM
for `yum`/`dnf`, an `APKBUILD` for `apk`, a `PKGBUILD` for the AUR
(`yay`).

## Tarball install

```console
curl -fsSL https://raw.githubusercontent.com/snitchbossdotcom/corelang/main/install.sh | bash
```

What the script does:

1. Runs `uname -m` to detect your architecture (x86_64, aarch64, or
   riscv64). Anything else, or a non-Linux system, is rejected.
2. Downloads the matching release tarball from GitHub releases.
3. Unpacks it to `/usr/local`: `core` goes to `/usr/local/bin`, the
   runtime and standard library to `/usr/local/lib/core/`.

`/usr/local` needs root, so the script uses `sudo` when it unpacks. Read
the script before piping it into a shell; that is always the right
reflex.

The tarballs are stored in the repository at `dist/`, so while the repo
is private the download needs authentication. Export a token with the
`repo` scope first:

```console
export GH_TOKEN=ghp_yourtoken
curl -H "Authorization: Bearer $GH_TOKEN" -fsSL \
  https://raw.githubusercontent.com/snitchbossdotcom/corelang/main/install.sh | bash
```

Once the repository is public, the plain one-liner works with no token.

Alternatives:

- Set `CORE_RELEASE_URL` to any location hosting the tarballs (a mirror,
  an internal server).
- Install without root: `CORE_INSTALL_PREFIX=$HOME/.local bash install.sh`.

Uninstall:

```console
CORE_UNINSTALL=1 bash install.sh
```

Verify the install:

```console
$ core version
Core compiler 0.1.0 (LLVM 18.1.x backend)
```

Then write your first program (see [getting-started.md](getting-started.md)
for the full walk-through):

```console
$ mkdir hello && cd hello
$ core init hello
$ core run
Hello, Core!
```

## Uninstalling

The install puts exactly two things on the system:

```console
sudo rm /usr/local/bin/core
sudo rm -r /usr/local/lib/core
```

Nothing else is written outside your home directory (the package cache
in `~/.cache/core`, if you used packages).

## Building from source

If there is no release for your machine, or you want to hack on the
compiler itself, build it. The prerequisites are a C/C++ toolchain,
CMake 3.20 or newer, and LLVM 18 development packages. LLVM 18 is the
one tested version; newer LLVM may work but is not guaranteed.

On Debian/Ubuntu:

```console
sudo apt install build-essential cmake git \
    llvm-18-dev libclang-18-dev clang libpolly-18-dev
```

Check that CMake can see LLVM:

```console
$ llvm-config-18 --version
18.1.x
```

Build:

```console
git clone https://github.com/snitchbossdotcom/corelang core
cd core
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

This produces the compiler (`build/core`) and the runtime object
(`build/corert.o`). Install them to the same locations the release
script uses:

```console
sudo cmake --install build
```

Run the test suite before trusting a self-built compiler:

```console
cd build && ctest --output-on-failure
```

The compiler finds its runtime (`corert.o`) and standard library next to
its own binary or in `/usr/local/lib/core`; both install paths work with
no configuration.
