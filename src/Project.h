// Core compiler - project system: core.toml manifest, core.lock lockfile,
// `core init/build/run/test/install/remove/update/list` commands, and the
// git-based package manager.
#ifndef CORE_PROJECT_H
#define CORE_PROJECT_H

#include "Driver.h"
#include "TOML.h"
#include <string>
#include <vector>

namespace core {

struct PackageRef {
  std::string name;    // import name (dependency key)
  std::string repo;    // git repo spec: github.com/user/repo or a URL / local path
  std::string version; // constraint, e.g. ^1.2.0 (may be empty)
  std::string resolvedVersion;
  std::string commit;
  std::string sha256;
};

struct Manifest {
  std::string name;
  std::string version = "0.1.0";
  std::string coreVersion;   // required compiler version ("" = any)
  std::string sourceDir = "src";
  std::vector<std::string> link;       // native libs to link (-l)
  std::vector<std::string> linkPaths;  // native lib search paths
  std::vector<std::string> linkArgs;   // raw linker args
  std::vector<PackageRef> dependencies; // from [dependencies]
};

struct LockEntry {
  std::string name;
  std::string version;
  std::string source;
  std::string commit;
  std::string sha256;
  std::vector<std::string> dependencies;
};

std::string manifestPath();   // core.toml in cwd
std::string lockfilePath();   // core.lock in cwd
bool loadManifest(const std::string &path, Manifest &out, Diagnostics &diag);
bool loadLockfile(const std::string &path, std::vector<LockEntry> &out);
bool saveLockfile(const std::string &path, const std::vector<LockEntry> &entries);

int cmdInit(const std::string &name, DriverOptions &opts);
bool setupProjectDriverForCheck(DriverOptions &opts, Manifest &m, Diagnostics &diag);
int cmdBuild(DriverOptions &opts);
int cmdRun(DriverOptions &opts, const std::vector<std::string> &args);
int cmdTest(DriverOptions &opts);
int cmdInstall(const std::string &spec, DriverOptions &opts);
int cmdRemove(const std::string &name, DriverOptions &opts);
int cmdUpdate(DriverOptions &opts);
int cmdList(DriverOptions &opts);

// Package cache helpers
std::string packageCacheDir();
bool fetchPackage(const std::string &spec, std::string &outCheckout, std::string &outVersion,
                  std::string &outCommit, Diagnostics &diag);

} // namespace core
#endif
