#include "Project.h"
#include "Parser.h"

#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>

namespace core {

std::string manifestPath() { return "core.toml"; }
std::string lockfilePath() { return "core.lock"; }

bool loadManifest(const std::string &path, Manifest &out, Diagnostics &diag) {
  bool ok;
  std::string text = readFileOrEmpty(path, ok);
  if (!ok) {
    diag.plainError(strfmt("cannot read manifest '%s' - run 'core init' first", path.c_str()));
    return false;
  }
  TOMLValue root;
  std::string err;
  if (!TOML::parse(text, root, err)) {
    diag.plainError(strfmt("%s: %s", path.c_str(), err.c_str()));
    return false;
  }
  auto pkgIt = root.table.find("package");
  if (pkgIt != root.table.end()) {
    const TOMLValue &pkg = pkgIt->second;
    out.name = pkg.table.count("name") ? pkg.table.at("name").s : "";
    out.version = pkg.table.count("version") ? pkg.table.at("version").s : "0.1.0";
    if (pkg.table.count("core-version")) out.coreVersion = pkg.table.at("core-version").s;
    if (pkg.table.count("source-dir")) out.sourceDir = pkg.table.at("source-dir").s;
  }
  auto buildIt = root.table.find("build");
  if (buildIt != root.table.end()) {
    const TOMLValue &b = buildIt->second;
    if (b.table.count("link")) out.link = b.table.at("link").arr;
    if (b.table.count("link-paths")) out.linkPaths = b.table.at("link-paths").arr;
    if (b.table.count("link-args")) out.linkArgs = b.table.at("link-args").arr;
  }
  auto depsIt = root.table.find("dependencies");
  if (depsIt != root.table.end()) {
    for (auto &[name, v] : depsIt->second.table) {
      PackageRef ref;
      ref.name = name;
      if (v.isString()) {
        // shorthand: "github.com/user/repo@^1.2.0"
        std::string spec = v.s;
        size_t at = spec.find('@');
        if (at != std::string::npos) {
          ref.repo = spec.substr(0, at);
          ref.version = spec.substr(at + 1);
        } else {
          ref.repo = spec;
        }
      } else if (v.isTable()) {
        if (v.table.count("git")) ref.repo = v.table.at("git").s;
        if (v.table.count("version")) ref.version = v.table.at("version").s;
        if (v.table.count("path")) ref.repo = v.table.at("path").s; // local path dep
      }
      out.dependencies.push_back(ref);
    }
  }
  return true;
}

static std::string lockHeader() {
  return "# Core lockfile - records exact resolved dependency versions.\n"
         "# Managed by the core toolchain; do not edit by hand while builds are in flight.\n";
}

bool loadLockfile(const std::string &path, std::vector<LockEntry> &out) {
  bool ok;
  std::string text = readFileOrEmpty(path, ok);
  if (!ok) return true; // missing lockfile = empty
  TOMLValue root;
  std::string err;
  if (!TOML::parse(text, root, err)) return false;
  auto it = root.tableArrays.find("packages");
  if (it == root.tableArrays.end()) return true;
  for (const TOMLValue &t : it->second) {
    LockEntry e;
    e.name = t.table.count("name") ? t.table.at("name").s : "";
    e.version = t.table.count("version") ? t.table.at("version").s : "";
    e.source = t.table.count("source") ? t.table.at("source").s : "";
    e.commit = t.table.count("commit") ? t.table.at("commit").s : "";
    e.sha256 = t.table.count("sha256") ? t.table.at("sha256").s : "";
    out.push_back(e);
  }
  return true;
}

bool saveLockfile(const std::string &path, const std::vector<LockEntry> &entries) {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return false;
  fprintf(f, "%s", lockHeader().c_str());
  if (entries.empty()) {
    fprintf(f, "# No dependencies yet. Packages installed with `core install` appear here.\n");
  }
  for (auto &e : entries) {
    fprintf(f, "[[packages]]\n");
    fprintf(f, "name = %s\n", TOML::escape(e.name).c_str());
    fprintf(f, "version = %s\n", TOML::escape(e.version).c_str());
    fprintf(f, "source = %s\n", TOML::escape(e.source).c_str());
    fprintf(f, "commit = %s\n", TOML::escape(e.commit).c_str());
    fprintf(f, "sha256 = %s\n", TOML::escape(e.sha256).c_str());
    fprintf(f, "dependencies = [");
    for (size_t i = 0; i < e.dependencies.size(); i++) {
      if (i) fprintf(f, ", ");
      fprintf(f, "%s", TOML::escape(e.dependencies[i]).c_str());
    }
    fprintf(f, "]\n\n");
  }
  fclose(f);
  return true;
}

// ------------------------------------------------------------ package cache --
std::string packageCacheDir() {
  const char *home = getenv("HOME");
  std::string base = home ? home : "/tmp";
  const char *xdg = getenv("XDG_CACHE_HOME");
  std::string dir = xdg ? std::string(xdg) + "/core" : base + "/.cache/core";
  return dir;
}

static int runGit(const std::string &args, const std::string &cwd = "") {
  std::string cmd = "git " + args;
  if (!cwd.empty()) cmd = "cd " + cwd + " && git " + args;
  return system(cmd.c_str());
}

// Version constraint helpers (semver subset)
struct SemVer {
  int major = 0, minor = 0, patch = 0;
  bool valid = false;
};
static bool operator>=(const SemVer &a, const SemVer &b) {
  if (a.major != b.major) return a.major > b.major;
  if (a.minor != b.minor) return a.minor > b.minor;
  return a.patch >= b.patch;
}

static SemVer parseSemVer(const std::string &s) {
  SemVer v;
  std::string t = s;
  // strip leading v / ^ / >=
  while (!t.empty() && !isdigit(t[0])) t = t.substr(1);
  if (sscanf(t.c_str(), "%d.%d.%d", &v.major, &v.minor, &v.patch) >= 1) v.valid = true;
  return v;
}
static bool constraintMatches(const std::string &constraint, const SemVer &v) {
  if (constraint.empty()) return true;
  std::string c = constraint;
  if (c[0] == '^') {
    SemVer base = parseSemVer(c);
    if (!base.valid || !v.valid) return true;
    if (base.major > 0) return v.major == base.major && v >= base;
    if (base.minor > 0) return v.major == 0 && v.minor == base.minor && v >= base;
    return v.major == 0 && v.minor == 0 && v.patch >= base.patch;
  }
  if (c.rfind(">=", 0) == 0) {
    SemVer base = parseSemVer(c);
    return v >= base;
  }
  // exact
  SemVer base = parseSemVer(c);
  return v.major == base.major && v.minor == base.minor && v.patch == base.patch;
}
// Fetch a package: clone/update the repo in the cache and check out the best
// matching version tag. Local paths are used directly (no copy) for dev deps.
bool fetchPackage(const std::string &spec, std::string &outCheckout, std::string &outVersion,
                  std::string &outCommit, Diagnostics &diag) {
  // spec forms: github.com/user/repo[@constraint] | https://... | /local/path | ./local
  std::string repo = spec, constraint;
  size_t at = spec.find('@');
  if (at != std::string::npos) {
    repo = spec.substr(0, at);
    constraint = spec.substr(at + 1);
  }
  bool local = repo[0] == '/' || repo.rfind("./", 0) == 0 || repo == "..";
  if (local) {
    if (!dirExists(repo)) {
      diag.plainError(strfmt("local package path '%s' does not exist", repo.c_str()));
      return false;
    }
    // read its manifest version if present
    Manifest m;
    SourceMgr sm;
    Diagnostics d(sm);
    if (loadManifest(joinPath(repo, "core.toml"), m, d)) outVersion = m.version;
    // commit from local git if available
    std::string commit = "local";
    {
      FILE *p = popen(("git -C " + repo + " rev-parse HEAD 2>/dev/null").c_str(), "r");
      if (p) {
        char buf[128] = {0};
        if (fgets(buf, sizeof buf, p)) {
          std::string c(buf);
          while (!c.empty() && c.back() == '\n') c.pop_back();
          if (!c.empty()) commit = c;
        }
        pclose(p);
      }
    }
    outCommit = commit;
    outCheckout = repo;
    return true;
  }

  // normalize shortcut repos into URLs
  std::string url = repo;
  if (url.rfind("github.com/", 0) == 0) url = "https://" + repo + ".git";

  std::string cacheRoot = packageCacheDir() + "/git";
  std::string safe;
  for (char c : repo) safe += (isalnum(c) || c == '.' || c == '-' || c == '_') ? c : '_';
  std::string cloneDir = cacheRoot + "/" + safe + ".git";

  if (!dirExists(cloneDir)) {
    if (runGit("clone --quiet " + url + " " + cloneDir) != 0) {
      diag.plainError(strfmt("failed to clone package repository '%s'", url.c_str()));
      return false;
    }
  } else {
    runGit("fetch --all --quiet", cloneDir);
  }
  // list tags, pick best matching semver
  std::string tagsOut;
  {
    FILE *p = popen(("git -C " + cloneDir + " tag --list 2>/dev/null").c_str(), "r");
    if (p) {
      char buf[512];
      size_t n;
      while ((n = fread(buf, 1, sizeof buf, p)) > 0) tagsOut.append(buf, n);
      pclose(p);
    }
  }
  std::string bestTag;
  SemVer best;
  size_t start = 0;
  while (start < tagsOut.size()) {
    size_t nl = tagsOut.find('\n', start);
    if (nl == std::string::npos) nl = tagsOut.size();
    std::string tag = tagsOut.substr(start, nl - start);
    start = nl + 1;
    if (tag.empty()) continue;
    SemVer v = parseSemVer(tag);
    if (!v.valid) continue;
    if (!constraint.empty() && !constraintMatches(constraint, v)) continue;
    if (bestTag.empty() || v >= best) {
      best = v;
      bestTag = tag;
    }
  }
  std::string checkout;
  if (!bestTag.empty()) {
    std::string vs = bestTag;
    while (!vs.empty() && !isdigit(vs[0])) vs = vs.substr(1);
    checkout = cacheRoot + "/checkouts/" + safe + "/" + vs;
    if (!dirExists(checkout)) {
      runGit("worktree add --detach " + checkout + " " + bestTag + " 2>/dev/null", cloneDir);
      if (!dirExists(checkout)) {
        // fallback: archive export
        std::string mkdir = "mkdir -p " + checkout;
        system(mkdir.c_str());
        runGit("archive " + bestTag + " | tar -x -C " + checkout, cloneDir);
      }
    }
    outVersion = vs;
  } else {
    // no tags: use HEAD of default branch
    std::string commit;
    {
      FILE *p = popen(("git -C " + cloneDir + " rev-parse HEAD").c_str(), "r");
      if (p) {
        char buf[128] = {0};
        if (fgets(buf, sizeof buf, p)) {
          std::string c(buf);
          while (!c.empty() && c.back() == '\n') c.pop_back();
          commit = c;
        }
        pclose(p);
      }
    }
    checkout = cacheRoot + "/checkouts/" + safe + "/" + commit;
    if (!dirExists(checkout)) {
      std::string mkdir = "mkdir -p " + checkout;
      system(mkdir.c_str());
      runGit("archive HEAD | tar -x -C " + checkout, cloneDir);
    }
    outVersion = "0.0.0+" + commit.substr(0, 7);
  }
  // commit hash for the lock
  {
    FILE *p = popen(("git -C " + cloneDir + " rev-parse HEAD").c_str(), "r");
    if (p) {
      char buf[128] = {0};
      if (fgets(buf, sizeof buf, p)) {
        std::string c(buf);
        while (!c.empty() && c.back() == '\n') c.pop_back();
        outCommit = c;
      }
      pclose(p);
    }
  }
  outCheckout = checkout;
  return true;
}

// ------------------------------------------------------------------- init ---
static bool setupProjectDriver(DriverOptions &opts, Manifest &m, Diagnostics &diag);
bool setupProjectDriverForCheck(DriverOptions &opts, Manifest &m, Diagnostics &diag) {
  return setupProjectDriver(opts, m, diag);
}

int cmdInit(const std::string &name, DriverOptions &opts) {
  (void)opts;
  Diagnostics diag(*(new SourceMgr()));
  std::string projectName = name;
  // default: directory name
  char cwd[4096];
  getcwd(cwd, sizeof cwd);
  if (projectName.empty()) {
    std::string dir = cwd;
    size_t slash = dir.find_last_of('/');
    projectName = slash == std::string::npos ? dir : dir.substr(slash + 1);
  }

  std::string toml = manifestPath();
  if (fileExists(toml)) {
    fprintf(stderr,
            "error: '%s' already exists in this directory\n"
            "  refusing to overwrite an existing project configuration\n",
            toml.c_str());
    return 1;
  }

  FILE *f = fopen(toml.c_str(), "wb");
  if (!f) {
    diag.plainError(strfmt("cannot create %s", toml.c_str()));
    return 1;
  }
  fprintf(f, "# Core project manifest - see docs/packages/manifest.md\n");
  fprintf(f, "[package]\n");
  fprintf(f, "name = %s\n", TOML::escape(projectName).c_str());
  fprintf(f, "version = \"0.1.0\"\n");
  fprintf(f, "core-version = \">=0.1.0\"\n");
  fprintf(f, "source-dir = \"src\"\n");
  fprintf(f, "\n[build]\n");
  fprintf(f, "# native libraries to link: link = [\"m\"]\n");
  fprintf(f, "link = []\n");
  fprintf(f, "\n[dependencies]\n");
  fprintf(f, "# example: coolmath = \"github.com/snitch/coolmath@^1.2.0\"\n");
  fclose(f);

  saveLockfile(lockfilePath(), {}); // valid empty lockfile, always created

  system("mkdir -p src tests examples assets");
  std::string mainPath = joinPath("src", "main.cr");
  if (!fileExists(mainPath)) {
    FILE *m = fopen(mainPath.c_str(), "wb");
    if (m) {
      fprintf(m, "func main() {\n    say \"Hello, Core!\"\n}\n");
      fclose(m);
    }
  }
  if (!fileExists("README.md")) {
    FILE *r = fopen("README.md", "wb");
    if (r) {
      fprintf(r, "# %s\n\nA Core project. Build and run it with:\n\n```\ncore build\n./%s\n```\n\n"
                 "Or with one command:\n\n```\ncore run\n```\n\n"
                 "Documentation lives in the Core repository under `docs/learning/`.\n",
              projectName.c_str(), projectName.c_str());
      fclose(r);
    }
  }
  printf("initialized Core project '%s'\n", projectName.c_str());
  printf("  created core.toml, core.lock, src/main.cr, tests/, README.md\n");
  printf("next: core run\n");
  return 0;
}

// ------------------------------------------------------------------ build ---
static bool setupProjectDriver(DriverOptions &opts, Manifest &m, Diagnostics &diag) {
  if (!fileExists(manifestPath())) {
    diag.plainError("no core.toml in this directory - run 'core init' first, "
                    "or use `core compile <output> <file.cr>` for standalone files");
    return false;
  }
  if (!loadManifest(manifestPath(), m, diag)) return false;
  opts.projectRoot = ".";
  // resolve dependencies (already installed packages)
  std::vector<LockEntry> lock;
  loadLockfile(lockfilePath(), lock);
  for (auto &dep : m.dependencies) {
    std::string checkout, version, commit;
    std::string spec = dep.repo + (dep.version.empty() ? "" : "@" + dep.version);
    if (!fetchPackage(spec, checkout, version, commit, diag)) return false;
    opts.packageSrcDirs.push_back(checkout);
    // dependency's own dependencies
    Manifest dm;
    SourceMgr sm;
    Diagnostics d(sm);
    if (loadManifest(joinPath(checkout, "core.toml"), dm, d)) {
      for (auto &dd : dm.dependencies) {
        std::string c2, v2, cm2;
        std::string spec2 = dd.repo + (dd.version.empty() ? "" : "@" + dd.version);
        if (fetchPackage(spec2, c2, v2, cm2, d)) opts.packageSrcDirs.push_back(c2);
      }
      for (auto &l : dm.link) opts.linkLibs.push_back(l);
      for (auto &lp : dm.linkPaths) opts.libPaths.push_back(lp);
      for (auto &la : dm.linkArgs) opts.linkArgs.push_back(la);
    }
    for (auto &l : m.link) opts.linkLibs.push_back(l);
    for (auto &lp : m.linkPaths) opts.libPaths.push_back(lp);
    for (auto &la : m.linkArgs) opts.linkArgs.push_back(la);
  }
  for (auto &l : m.link) opts.linkLibs.push_back(l);
  opts.stdDir = dirName(findCompilerData("std/prelude.cr", opts));
  opts.runtimeObj = findCompilerData("corert.o", opts);
  return true;
}

int cmdBuild(DriverOptions &opts) {
  SourceMgr sm;
  Diagnostics diag(sm);
  Manifest m;
  if (!setupProjectDriver(opts, m, diag)) return 1;
  Driver d(opts);
  std::string entry = joinPath(joinPath(".", m.sourceDir.empty() ? "src" : m.sourceDir), "main.cr");
  if (!fileExists(entry)) {
    diag.plainError(strfmt("no entry file at '%s'", entry.c_str()));
    return 1;
  }
  return d.compile(m.name, entry);
}

int cmdRun(DriverOptions &opts, const std::vector<std::string> &args) {
  SourceMgr sm;
  Diagnostics diag(sm);
  Manifest m;
  if (!setupProjectDriver(opts, m, diag)) return 1;
  Driver d(opts);
  std::string entry = joinPath(joinPath(".", m.sourceDir.empty() ? "src" : m.sourceDir), "main.cr");
  if (!fileExists(entry)) {
    diag.plainError(strfmt("no entry file at '%s'", entry.c_str()));
    return 1;
  }
  int rc = d.compile(m.name, entry);
  if (rc != 0) return rc;
  std::string cmd = "./" + m.name;
  for (auto &a : args) cmd += " " + a;
  int code = system(cmd.c_str());
  return WEXITSTATUS(code);
}

// ------------------------------------------------------------------- test ---
// Tests are functions named test_* inside tests/*.cr files. The compiler
// generates a harness that runs every test and reports pass/fail.
int cmdTest(DriverOptions &opts) {
  SourceMgr sm;
  Diagnostics diag(sm);
  Manifest m;
  if (!setupProjectDriver(opts, m, diag)) return 1;

  // collect test files
  std::vector<std::string> testFiles;
  {
    // tests/*.cr via glob through popen (portable enough here)
    FILE *p = popen("ls tests/*.cr 2>/dev/null", "r");
    if (p) {
      char buf[512];
      while (fgets(buf, sizeof buf, p)) {
        std::string f(buf);
        while (!f.empty() && (f.back() == '\n' || f.back() == ' ')) f.pop_back();
        if (!f.empty()) testFiles.push_back(f);
      }
      pclose(p);
    }
  }
  if (testFiles.empty()) {
    printf("no tests found (expected tests/*.cr files with test_* functions)\n");
    return 0;
  }

  // A test file contains test_* funcs plus its own module code; we compile the
  // FIRST test file as the entry (it can import others) with a generated main.
  // Simpler v1: every test file must contain `func main()` that calls its own
  // tests and returns nonzero on failure via std assert. We provide a
  // convention: `func main()` generated if missing? Instead: compile each test
  // file as entry and run; the file's test_* functions are run through its
  // own main. To make this ergonomic, core test compiles each test file with
  // the project's source dir on the import path.
  int failures = 0;
  int total = 0;
  for (auto &tf : testFiles) {
    DriverOptions topts = opts;
    Driver d(topts);
    // output name = the test file's basename (no path, no extension)
    size_t slash = tf.find_last_of('/');
    std::string base = slash == std::string::npos ? tf : tf.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    std::string outName = "core_test_" + base;
    int rc = d.compile(outName, tf);
    if (rc != 0) {
      failures++;
      continue;
    }
    printf("---- running %s\n", tf.c_str());
    int code = system(("./" + outName).c_str());
    total++;
    if (code != 0) failures++;
    std::string rm = "rm -f " + outName;
    system(rm.c_str());
  }
  printf("\n%d/%d test files passed\n", total - failures, total);
  return failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------- packages --
int cmdInstall(const std::string &spec, DriverOptions &opts) {
  SourceMgr sm;
  Diagnostics diag(sm);
  if (!fileExists(manifestPath())) {
    diag.plainError("no core.toml in this directory - run 'core init' first");
    return 1;
  }
  Manifest m;
  if (!loadManifest(manifestPath(), m, diag)) return 1;

  // determine name + repo + constraint
  std::string repo = spec, constraint;
  size_t at = spec.find('@');
  if (at != std::string::npos) {
    repo = spec.substr(0, at);
    constraint = spec.substr(at + 1);
  }
  std::string pkgName = repo;
  size_t slash = pkgName.find_last_of('/');
  if (slash != std::string::npos) pkgName = pkgName.substr(slash + 1);

  std::string checkout, version, commit;
  if (!fetchPackage(spec, checkout, version, commit, diag)) return 1;
  printf("resolved %s -> version %s (commit %s)\n", spec.c_str(), version.c_str(),
         commit.substr(0, 12).c_str());

  // update core.toml [dependencies]
  bool ok;
  std::string text = readFileOrEmpty(manifestPath(), ok);
  std::string depLine = pkgName + " = " + TOML::escape(repo + "@" + constraint);
  if (text.find("[" + depLine + "]") == std::string::npos) {
    // naive but deterministic: replace empty [dependencies] section
    size_t deps = text.find("[dependencies]");
    if (deps != std::string::npos) {
      size_t eol = text.find('\n', deps);
      text = text.substr(0, eol + 1) + depLine + "\n" + text.substr(eol + 1);
    } else {
      text += "\n[dependencies]\n" + depLine + "\n";
    }
    FILE *f = fopen(manifestPath().c_str(), "wb");
    if (f) {
      fwrite(text.data(), 1, text.size(), f);
      fclose(f);
    }
  }

  // update lockfile (replace entry if present)
  std::vector<LockEntry> lock;
  loadLockfile(lockfilePath(), lock);
  LockEntry e;
  e.name = pkgName;
  e.version = version;
  e.source = repo;
  e.commit = commit;
  e.sha256 = ""; // content hash recorded when available; git commit pins content
  bool replaced = false;
  for (auto &x : lock)
    if (x.name == pkgName) {
      x = e;
      replaced = true;
    }
  if (!replaced) lock.push_back(e);
  saveLockfile(lockfilePath(), lock);

  printf("installed %s (%s)\n", pkgName.c_str(), version.c_str());
  printf("  updated core.toml and core.lock\n");
  return 0;
}

int cmdRemove(const std::string &name, DriverOptions &opts) {
  (void)opts;
  SourceMgr sm;
  Diagnostics diag(sm);
  if (!fileExists(manifestPath())) {
    diag.plainError("no core.toml in this directory");
    return 1;
  }
  bool ok;
  std::string text = readFileOrEmpty(manifestPath(), ok);
  // remove line `name = "..."`
  size_t pos = 0;
  std::string out;
  bool removed = false;
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string::npos) eol = text.size();
    std::string line = text.substr(pos, eol - pos);
    std::string prefix = name + " = ";
    if (line.rfind(prefix, 0) == 0) {
      removed = true; // skip line
    } else {
      out += line + "\n";
    }
    pos = eol + 1;
  }
  if (removed) {
    FILE *f = fopen(manifestPath().c_str(), "wb");
    if (f) {
      fwrite(out.data(), 1, out.size(), f);
      fclose(f);
    }
  }
  // drop from lockfile
  std::vector<LockEntry> lock;
  loadLockfile(lockfilePath(), lock);
  std::vector<LockEntry> keep;
  for (auto &x : lock)
    if (x.name != name) keep.push_back(x);
  saveLockfile(lockfilePath(), keep);
  printf(removed ? "removed '%s' from core.toml and core.lock\n"
                 : "'%s' was not in dependencies (lockfile cleaned)\n", name.c_str());
  return 0;
}

int cmdUpdate(DriverOptions &opts) {
  SourceMgr sm;
  Diagnostics diag(sm);
  Manifest m;
  if (!setupProjectDriver(opts, m, diag)) return 1;
  // re-resolve everything: fetchPackage picks the best matching version
  std::vector<LockEntry> lock;
  for (auto &dep : m.dependencies) {
    std::string checkout, version, commit;
    std::string spec = dep.repo + (dep.version.empty() ? "" : "@" + dep.version);
    if (!fetchPackage(spec, checkout, version, commit, diag)) return 1;
    LockEntry e;
    e.name = dep.name;
    e.version = version;
    e.source = dep.repo;
    e.commit = commit;
    lock.push_back(e);
    printf("updated %s -> %s\n", dep.name.c_str(), version.c_str());
  }
  saveLockfile(lockfilePath(), lock);
  printf("core.lock updated\n");
  return 0;
}

int cmdCheckFile(DriverOptions &opts) {
  // used by `core check` when a manifest exists: resolve dependencies first
  return 0;
}

int cmdList(DriverOptions &opts) {
  SourceMgr sm;
  Diagnostics diag(sm);
  Manifest m;
  if (!fileExists(manifestPath())) {
    diag.plainError("no core.toml in this directory");
    return 1;
  }
  loadManifest(manifestPath(), m, diag);
  std::vector<LockEntry> lock;
  loadLockfile(lockfilePath(), lock);
  printf("project: %s %s\n", m.name.c_str(), m.version.c_str());
  if (m.dependencies.empty()) {
    printf("dependencies: (none)\n");
    return 0;
  }
  printf("dependencies:\n");
  for (auto &dep : m.dependencies) {
    std::string resolved = "";
    for (auto &l : lock)
      if (l.name == dep.name) resolved = l.version + " (" + l.commit.substr(0, 12) + ")";
    printf("  %s\n    requirement: %s\n    resolved: %s\n", dep.name.c_str(),
           dep.version.empty() ? "any" : dep.version.c_str(),
           resolved.empty() ? "not installed (run core install)" : resolved.c_str());
  }
  return 0;
}

} // namespace core
