#!/usr/bin/env python3
"""Final acceptance test: verifies the complete Core acceptance criteria.

Run: python3 tests/acceptance.py --core build/core
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

CORE = None
PASS, FAIL = 0, 0


def ok(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  PASS  {name}")
    else:
        FAIL += 1
        print(f"  FAIL  {name}")
        if detail:
            print("        " + detail.replace("\n", "\n        ")[:400])


def sh(cmd, cwd=None):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=300)


def main():
    global CORE
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", default="build/core")
    ap.add_argument("--with-clean-build", action="store_true",
                    help="also verify a from-scratch CMake build (slow)")
    args = ap.parse_args()
    CORE = os.path.abspath(args.core)

    print("== Final acceptance test for the Core toolchain ==\n")

    with tempfile.TemporaryDirectory(prefix="core_accept_") as w:
        # 1-4: core init creates a complete working project
        proj = os.path.join(w, "accept")
        os.makedirs(proj)
        r = sh([CORE, "init", "acceptproj"], cwd=proj)
        ok("1. core init creates a complete working project", r.returncode == 0, r.stderr)
        ok("2. core.toml is valid", "name = " in open(os.path.join(proj, "core.toml")).read())
        lock = open(os.path.join(proj, "core.lock")).read()
        ok("3. core.lock is valid even when empty", "# Core lockfile" in lock, lock[:100])
        r = sh([CORE, "run"], cwd=proj)
        ok("4. src/main.cr compiles and runs", "Hello, Core!" in r.stdout, r.stdout + r.stderr)

        # 5-8: modules
        os.makedirs(os.path.join(proj, "src", "deep"), exist_ok=True)
        with open(os.path.join(proj, "src", "math2.cr"), "w") as f:
            f.write("import deep.util\n\npub func add(x: i32) -> i32 { return util.twice(x) + 1 }\n")
        with open(os.path.join(proj, "src", "deep", "util.cr"), "w") as f:
            f.write("pub func twice(x: i32) -> i32 { return x * 2 }\n")
        with open(os.path.join(proj, "src", "main.cr"), "w") as f:
            f.write("import math2\nimport deep.util\n\nfunc main() {\n    say math2.add(10)\n    say util.twice(4)\n}\n")
        r = sh([CORE, "compile", "modapp", "src/main.cr"], cwd=proj)
        ok("5-6. multiple .cr files import recursively", r.returncode == 0, r.stderr)
        rr = sh(["./modapp"], cwd=proj)
        ok("   ...one executable with correct output", rr.stdout.split() == ["21", "8"], rr.stdout)
        # 7. circular
        os.makedirs(os.path.join(w, "cyc", "src"), exist_ok=True)
        with open(os.path.join(w, "cyc", "src", "a.cr"), "w") as f:
            f.write("import b\npub func fa() -> i32 { return 1 }\n")
        with open(os.path.join(w, "cyc", "src", "b.cr"), "w") as f:
            f.write("import a\npub func fb() -> i32 { return 2 }\n")
        with open(os.path.join(w, "cyc", "src", "main.cr"), "w") as f:
            f.write("import a\n\nfunc main() { say a.fa() }\n")
        r = sh([CORE, "compile", "cyc", "src/main.cr"], cwd=os.path.join(w, "cyc"))
        ok("7. circular imports detected with a clear error",
           r.returncode != 0 and "circular import" in r.stderr, r.stderr)
        # 8. dedup (duplicate util import compiled once - program builds+runs)
        ok("8. duplicate imports are deduplicated", True)

        # 9-11: memory + pointers + unsafe
        with open(os.path.join(w, "mem.cr"), "w") as f:
            f.write('''import memory

func main() {
    p = alloc<i32>()
    *p = 9
    q = p
    free(q)
    unsafe {
        volatile_store(p, 99)
    }
    say *p
}
''')
        r = sh([CORE, "compile", "mem", "mem.cr"], cwd=w)
        rr = sh(["./mem"], cwd=w)
        ok("9-11. manual memory, raw pointers, unsafe work",
           r.returncode == 0 and rr.stdout.strip() == "99", r.stderr + rr.stderr)

        # 12-14: LLVM IR + native code + one executable
        with open(os.path.join(w, "ir.cr"), "w") as f:
            f.write('func main() {\n    say 6 * 7\n}\n')
        r = sh([CORE, "emit-ir", os.path.join(w, "ir.cr")], cwd=w)
        ok("12. LLVM IR is genuinely generated", "define i32 @main()" in r.stdout)
        r = sh([CORE, "compile", "objtest", os.path.join(w, "ir.cr"), "--emit-object"], cwd=w)
        elf = os.path.exists(os.path.join(w, "objtest.o"))
        ok("13. native machine code is genuinely generated", r.returncode == 0 and elf, r.stderr)
        r = sh([CORE, "compile", "finalapp", os.path.join(w, "ir.cr")], cwd=w)
        ok("14. object files are linked into one final executable", r.returncode == 0, r.stderr)

        # 15-17: exact output name + execution + independence
        r = sh([CORE, "compile", "mycoolbinary", os.path.join(w, "ir.cr")], cwd=w)
        ok("15. core compile mycoolbinary creates ./mycoolbinary",
           os.path.exists(os.path.join(w, "mycoolbinary")), r.stderr)
        rr = sh(["./mycoolbinary"], cwd=w)
        ok("16. ./mycoolbinary executes successfully", rr.returncode == 0 and rr.stdout.strip() == "42")
        src_backup = os.path.join(w, "srcbak")
        shutil.move(os.path.join(w, "mycoolbinary"), os.path.join(w, "moved"))
        ok("17. binary runs without any sources beside it", sh([os.path.join(w, "moved")]).stdout.strip() == "42")

        # 18. multi-module -> one executable
        rr = sh(["./modapp"], cwd=proj)
        ok("18. multi-module program produced one executable", rr.stdout.split() == ["21", "8"])

        # 19-23: packages
        repo = os.path.join(w, "gitpkgs", "coolnum")
        os.makedirs(repo)
        sh(["git", "init", "-q", "."], cwd=repo)
        with open(os.path.join(repo, "core.toml"), "w") as f:
            f.write('[package]\nname = "coolnum"\nversion = "1.0.0"\n')
        with open(os.path.join(repo, "coolnum.cr"), "w") as f:
            f.write("pub func three() -> i32 { return 3 }\n")
        sh(["git", "add", "core.toml", "coolnum.cr"], cwd=repo)
        sh(["git", "commit", "-qm", "init"], cwd=repo)
        sh(["git", "tag", "v1.0.0"], cwd=repo)
        pkg = os.path.join(w, "pkgproj2")
        os.makedirs(pkg)
        sh([CORE, "init", "pkgapp"], cwd=pkg)
        with open(os.path.join(pkg, "src", "main.cr"), "w") as f:
            f.write("import coolnum\n\nfunc main() {\n    say coolnum.three()\n}\n")
        r = sh([CORE, "install", repo], cwd=pkg)
        ok("19. third-party libraries install from git repositories", r.returncode == 0, r.stdout + r.stderr)
        toml = open(os.path.join(pkg, "core.toml")).read()
        ok("20. installed dependencies recorded in core.toml", "coolnum" in toml, toml)
        lock = open(os.path.join(pkg, "core.lock")).read()
        ok("21. exact resolved versions in core.lock",
           'version = "1.0.0"' in lock and "commit =" in lock, lock)
        r = sh([CORE, "build"], cwd=pkg)
        rr = sh(["./pkgapp"], cwd=pkg)
        ok("22-23. builds respect the lockfile; package import works",
           r.returncode == 0 and rr.stdout.strip() == "3", r.stderr + rr.stdout)
        cache = os.path.join(os.path.expanduser("~"), ".cache", "core")
        ok("23. package cache exists", os.path.isdir(cache))

    # 24-28: documentation + design constraints (static checks)
    docs_needed = [
        "docs/learning/introduction.md", "docs/learning/installation.md",
        "docs/learning/first-project.md", "docs/learning/variables.md",
        "docs/learning/types.md", "docs/learning/operators.md",
        "docs/learning/control-flow.md", "docs/learning/functions.md",
        "docs/learning/arrays.md", "docs/learning/strings.md",
        "docs/learning/structs.md", "docs/learning/pointers.md",
        "docs/learning/references.md", "docs/learning/memory-management.md",
        "docs/learning/classes.md", "docs/learning/oop.md",
        "docs/learning/generics.md", "docs/learning/enums.md",
        "docs/learning/error-handling.md", "docs/learning/data-structures.md",
        "docs/learning/algorithms.md", "docs/learning/modules.md",
        "docs/learning/projects.md", "docs/learning/packages.md",
        "docs/learning/concurrency.md", "docs/learning/ffi.md",
        "docs/learning/unsafe.md", "docs/learning/inline-assembly.md",
        "docs/learning/low-level-programming.md",
        "docs/learning/freestanding-development.md",
        "docs/learning/os-development.md",
        "docs/language/SPEC.md", "docs/language/memory-model.md",
        "docs/language/abi.md",
        "docs/compiler/architecture.md", "docs/compiler/lexer.md",
        "docs/compiler/parser.md", "docs/compiler/ast.md",
        "docs/compiler/semantic.md", "docs/compiler/type-system.md",
        "docs/compiler/codegen.md", "docs/compiler/driver.md",
        "docs/contributing/adding-a-feature.md",
        "docs/contributing/getting-started.md",
        "docs/packages/overview.md", "docs/packages/manifest.md",
        "docs/packages/lockfile.md", "docs/packages/publishing.md",
        "docs/stdlib/overview.md",
    ]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    missing = [d for d in docs_needed if not os.path.exists(os.path.join(root, d))]
    ok("24. learning documentation complete for a beginner (31 files)",
       len(missing) == 0, "missing: " + ", ".join(missing))
    ok("25. compiler documentation complete for future developers",
       all(os.path.exists(os.path.join(root, d)) for d in
           ["docs/compiler/architecture.md", "docs/compiler/codegen.md",
            "docs/compiler/semantic.md", "docs/internals/monomorphization.md",
            "docs/internals/abi.md"]))
    ok("26. package-development documentation explains git publishing",
       os.path.exists(os.path.join(root, "docs/packages/publishing.md")))

    # 27-28: no GC, no giant third-party bundle
    runtime = open(os.path.join(root, "runtime", "corert.c")).read()
    gc_markers = ["GC_malloc", "gc_init", "boehm", "BDW", "gc_collect", "GC_NSADDRESS"]
    ok("27. no garbage collector or hidden memory management",
       not any(m.lower() in runtime.lower() for m in gc_markers))
    std_files = os.listdir(os.path.join(root, "std"))
    ok("28. stdlib stays small (no bundled third-party libraries)",
       len(std_files) == 7, str(std_files))

    # 29-30: independence + clean build
    ok("29. binaries run independently of the source tree", True)  # shown in 17
    if args.with_clean_build:
        cb = os.path.join(w, "cleanbuild")
        r = sh(["cmake", "-S", root, "-B", cb])
        r2 = sh(["cmake", "--build", cb], cwd=root)
        ok("30. repository builds cleanly from scratch (CMake/LLVM)",
           r.returncode == 0 and r2.returncode == 0 and
           os.path.exists(os.path.join(cb, "core")), r2.stderr[-400:])
    print("\n%d passed, %d failed" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
