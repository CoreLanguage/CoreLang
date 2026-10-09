Name:           core
Version:        0.1.0
Release:        1%{?dist}
Summary:        Core compiled systems programming language
License:        MIT
URL:            https://github.com/snitchbossdotcom/corelang
# Build from the release tarball (ships a static binary, no LLVM needed):
Source0:        core-linux-%{_arch}.tar.gz

BuildArch:      x86_64
# (separate aarch64/riscv64 builds use the matching tarball)

%description
Core is a compiled, statically typed systems language with an LLVM
backend, manual memory management and a minimal C runtime.

%install
mkdir -p %{buildroot}%{_prefix}/bin %{buildroot}%{_prefix}/lib
tar -xzf %{SOURCE0} -C %{buildroot}%{_prefix} --strip-components=0
# tarball has bin/ lib/ at top level; prefix layout = /usr/local equivalent
mkdir -p %{buildroot}%{_bindir}
ln -sf %{_prefix}/bin/core %{buildroot}%{_bindir}/core || true

%files
%{_prefix}/bin/core
%{_prefix}/lib/core/
%{_bindir}/core

%changelog
* Fri Oct 09 2026 Core Team <packages@core-lang.example> - 0.1.0-1
- Initial RPM (works with both yum and dnf).
