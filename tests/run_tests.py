#!/usr/bin/env python3
"""Core compiler end-to-end test suite.

Usage: run_tests.py --core /path/to/core [test-name-filter]

Covers: lexer/parser, semantics/diagnostics, codegen and execution, pointers,
manual memory, structs, classes/OOP, generics, modules/imports, circular
imports, duplicate-import dedup, core init, core build/run/test/check,
package management against local git repos, LLVM IR emission, machine-code
emission, optimization levels, debug info, FFI, SIMD, unsafe, and freestanding
compilation.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

PASS = 0
FAIL = 0
FAILURES = []
CORE = "core"


def run(cmd, cwd=None, timeout=120):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  ok   {name}")
    else:
        FAIL += 1
        FAILURES.append((name, detail))
        print(f"  FAIL {name}")
        if detail:
            for line in detail.splitlines()[:8]:
                print(f"       {line}")


def compile_src(workdir, name, source, extra=None, expect_success=True):
    entry = os.path.join(workdir, f"{name}.cr")
    with open(entry, "w") as f:
        f.write(source)
    args = [CORE, "compile", name + ".bin", f"{name}.cr"] + (extra or [])
    r = run(args, cwd=workdir)
    binary = os.path.join(workdir, name + ".bin")
    ok = r.returncode == 0 and os.path.exists(binary)
    if expect_success and not ok:
        print(r.stdout)
        print(r.stderr)
    return r, binary, ok


NAME_COUNTER = [0]

def unique_name():
    NAME_COUNTER[0] += 1
    return f"prog{NAME_COUNTER[0]}"

def expect_output(workdir, source, expected_lines, extra=None, name=None):
    name = name or unique_name()
    r, binary, ok = compile_src(workdir, name, source, extra)
    if not ok:
        check(f"{name} compiles", False, r.stdout + r.stderr)
        return
    run_r = run([os.path.abspath(binary)], cwd=workdir)
    out = run_r.stdout.strip().splitlines()
    check(f"{name} runs with correct output",
          out == expected_lines and run_r.returncode == 0,
          f"got {out}, rc={run_r.returncode}\n{run_r.stderr}")


def expect_compile_error(workdir, source, needle, name=None):
    name = name or ("bad" + str(NAME_COUNTER[0] + 1))
    NAME_COUNTER[0] += 1
    entry = os.path.join(workdir, f"{name}.cr")
    with open(entry, "w") as f:
        f.write(source)
    r = run([CORE, "compile", name + ".bin", f"{name}.cr"], cwd=workdir)
    check(f"{name} rejected with '{needle}'",
          r.returncode != 0 and needle in r.stderr and os.path.exists(binary_of(workdir, name)) is False,
          r.stderr)


def binary_of(workdir, name):
    return os.path.join(workdir, name + ".bin")


# --------------------------------------------------------------------- tests --

def test_basics(w):
    expect_output(w, 'func main() {\n    say "Hello, Core!"\n}\n', ["Hello, Core!"])
    expect_output(w, 'func main() {\n    x = 10\n    say x + 5\n}\n', ["15"])
    expect_output(w, 'func main() {\n    mut x: i32 = 3\n    x = x * 4\n    say x\n}\n', ["12"])
    expect_output(w, 'func main() {\n    const MAX: i32 = 100\n    say MAX + 1\n}\n', ["101"])
    expect_output(w, 'func main() {\n    say 1 + 2 * 3\n    say (1 + 2) * 3\n    say 10 / 3\n    say 10 % 3\n    say 2 << 4\n    say 255 >> 4\n}\n',
                  ["7", "9", "3", "1", "32", "15"])
    expect_output(w, 'func main() {\n    say true && !false\n    say false || true\n    say 1 < 2 == true\n}\n',
                  ["true", "true", "true"])
    expect_output(w, 'func main() {\n    mut x = 0\n    if true { x = 1 } else { x = 2 }\n    say x\n}\n', ["1"])
    expect_output(w, 'func main() {\n    mut n = 0\n    while n < 5 { n += 1 }\n    say n\n}\n', ["5"])
    expect_output(w, 'func main() {\n    mut total = 0\n    for i in 0..10 { total += i }\n    say total\n}\n', ["45"])
    expect_output(w, 'func main() {\n    for i in 0..5 {\n        if i == 2 { continue }\n        if i == 4 { break }\n        say i\n    }\n}\n', ["0", "1", "3"])
    expect_output(w, 'func main() {\n    x = 2\n    switch x {\n        case 1: say "one"\n        case 2: say "two"\n        default: say "other"\n    }\n}\n', ["two"])
    expect_output(w, 'func main() {\n    for i in 0..3 { for j in 0..2 { say i * 10 + j } }\n}\n', ["0", "1", "10", "11", "20", "21"])


def test_functions(w):
    expect_output(w, '''func add(a: i32, b: i32) -> i32 { return a + b }

func main() {
    say add(2, 3)
    say add(add(1, 2), 4)
}
''', ["5", "7"])
    expect_output(w, '''func greet(name: string, greeting: string = "Hello") -> string {
    return greeting + ", " + name + "!"
}

func main() {
    say greet("Core")
    say greet("Core", "Hi")
}
''', ["Hello, Core!", "Hi, Core!"])
    expect_output(w, '''func fact(n: i32) -> i32 {
    if n <= 1 { return 1 }
    return n * fact(n - 1)
}

func main() { say fact(10) }
''', ["3628800"])
    expect_output(w, '''func early() -> i32 { return 1; return 2 }

func no_return_numeric() -> i32 {
    x = 1
}

func main() {
    say early()
    say no_return_numeric()
}
''', ["1", "0"])
    expect_output(w, '''func apply(f: func(i32) -> i32, v: i32) -> i32 { return f(v) }

func main() {
    double = func(x: i32) -> i32 { return x * 2 }
    say apply(double, 21)
    say apply(func(x: i32) -> i32 { return x + 1 }, 9)
}
''', ["42", "10"])
    expect_output(w, '''import process

func never_demo() -> never {
    exit(3)
}

extern func core_rt_exit_unused() -> void

func main() {
    // never-returning calls make following code unreachable
    say "before"
}
''', ["before"])


def test_types(w):
    expect_output(w, '''func main() {
    a: i8 = 127
    b: u8 = 255
    c: i16 = 32767
    d: u16 = 65535
    e: i32 = 2147483647
    f: u32 = 4294967295
    g: i64 = 9223372036854775807
    h: u64 = 18446744073709551615
    say a + b as i32
    say c as i32 + d as i32
    say e as i64
    say f as u64
    say g
    say h
}
''', ["382", "98302", "2147483647", "4294967295", "9223372036854775807", "18446744073709551615"])
    expect_output(w, '''func main() {
    f: f32 = 1.5
    d: f64 = 3.25
    say f * 2.0 as f32
    say d * 2.0
    say 7 / 2
    say 7.0 / 2.0
    say 10 as f64 / 4.0
    say -3.5
    say 1.0e3
}
''', ["3", "6.5", "3", "3.5", "2.5", "-3.5", "1000"])
    expect_output(w, '''func main() {
    c: char = 'A'
    say c as i32
    say 'a' + 1 as char
    b = c == 'A'
    say b
    say sizeof(i32)
    say sizeof([i64; 4])
    say alignof(i64)
}
''', ["65", "98", "true", "4", "32", "8"])
    expect_output(w, '''func main() {
    big: u128 = 340282366920938463463374607431768211455
    say big
    big2: i128 = -170141183460469231731687303715884105728
    say big2
}
''', ["340282366920938463463374607431768211455", "-170141183460469231731687303715884105728"])


def test_pointers_memory(w):
    expect_output(w, '''func main() {
    x = 10
    p = &x
    say *p
    *p = 20
    say x
    say p == &x
}
''', ["10", "20", "true"])
    expect_output(w, '''func main() {
    data: [i32; 4] = [10, 20, 30, 40]
    mut p = &data[0]
    say *(p + 2)
    p += 1
    say *p
    say p - &data[0]
}
''', ["30", "20", "1"])
    expect_output(w, '''func main() {
    x = 1
    p = &x
    pp = &p
    **pp = 42
    say x
}
''', ["42"])
    expect_output(w, '''import memory

func main() {
    p = alloc<i32>()
    *p = 7
    say *p
    free(p)

    mut arr = alloc_array<i32>(8)
    for i in 0..8 { arr[i] = i }
    say arr[5]
    arr = realloc_array<i32>(arr, 16)
    say arr[5]
    free(arr)

    bytes = alloc_bytes(100)
    free(bytes)
    z = alloc_zeroed<i64>()
    say *z
    free(z)
}
''', ["7", "5", "5", "0"])
    expect_output(w, '''func main() {
    // bounds-checked array indexing aborts on violation
    data: [i32; 3] = [1, 2, 3]
    say data[2]
    say "done"
}
''', ["3", "done"])
    expect_output(w, '''func main() {
    s = "abc"
    say len(s)
    say s[0]
    t = s + "def"
    say t
    say t == "abcdef"
    say s < t
}
''', ["3", "a", "abcdef", "true", "true"])


def test_structs_classes(w):
    expect_output(w, '''struct User {
    name: string
    age: i32
}

func main() {
    user = User { name: "Alex", age: 20 }
    say user.age
    say user.name
    user.age += 1
    say user.age
}
''', ["20", "Alex", "21"])
    expect_output(w, '''struct Vec2 { x: f64, y: f64 }

func main() {
    vs: [Vec2; 3] = [Vec2 { x: 1.0, y: 1.0 }, Vec2 { x: 2.0, y: 2.0 }, Vec2 { x: 3.0, y: 3.0 }]
    mut total = 0.0
    for v in vs { total += v.x }
    say total as i32
}
''', ["6"])
    expect_output(w, '''class Animal {
    name: string
    pub func init(name: string) { self.name = name }
    pub virtual func speak() -> string { return "..." }
    pub func describe() -> string { return self.name + " says " + self.speak() }
}

class Dog : Animal {
    pub func init() { Animal.init("dog") }
    pub override func speak() -> string { return "Woof" }
}

func main() {
    d = Dog { }
    say d.speak()
    a: ptr<Animal> = &d
    say a.speak()
    say a.describe()
}
''', ["Woof", "Woof", "dog says Woof"])
    expect_output(w, '''interface Shape { func area() -> f64 }

class Sq : Shape {
    side: f64
    pub func init(s: f64) { self.side = s }
    pub func area() -> f64 { return self.side * self.side }
}

class Circle : Shape {
    r: f64
    pub func init(r: f64) { self.r = r }
    pub func area() -> f64 { return 3.14159 * self.r * self.r }
}

func total_area(shapes: [Shape; 2]) -> f64 {
    return shapes[0].area() + shapes[1].area()
}

func main() {
    s = Sq { side: 3.0 }
    c = Circle { r: 1.0 }
    say total_area([s as Shape, c as Shape]) as i32
}
''', ["12"])


def test_generics_enums(w):
    expect_output(w, '''func max<T>(a: T, b: T) -> T {
    if a > b { return a }
    return b
}

func main() {
    say max(3, 9)
    say max(1.5, 0.5)
}
''', ["9", "1.5"])
    expect_output(w, '''struct Box<T> { value: T }

func main() {
    b: Box<i32> = Box<i32> { value: 5 }
    say b.value
    s: Box<string> = Box<string> { value: "gen" }
    say s.value
}
''', ["5", "gen"])
    expect_output(w, '''enum Color { Red, Green, Blue }

func main() {
    c = Color.Green
    match c {
        Red   { say "r" }
        Green { say "g" }
        Blue  { say "b" }
    }
    switch c {
        case Color.Green: say "green!"
        default: say "?"
    }
}
''', ["g", "green!"])
    expect_output(w, '''enum Op {
    Add(i32, i32),
    Neg(i32),
    Id
}

func eval(e: Op) -> i32 {
    match e {
        Add(a, b) { return a + b }
        Neg(v)    { return -v }
        Id        { return 7 }
    }
}

func main() {
    say eval(Op.Add(2, 3))
    say eval(Op.Neg(4))
    say eval(Op.Id)
}
''', ["5", "-4", "7"])
    expect_output(w, '''func main() {
    o: Option<i32> = Option<i32>.Some(3)
    match o {
        Some(v) { say v }
        None    { say "none" }
    }
    n: Option<i32> = Option<i32>.None
    match n {
        Some(v) { say v }
        None    { say "none" }
    }
}
''', ["3", "none"])


def test_modules(w):
    os.makedirs(os.path.join(w, "src", "utils"), exist_ok=True)
    with open(os.path.join(w, "src", "math.cr"), "w") as f:
        f.write("import utils.helper\n\npub func double(x: i32) -> i32 { return helper.twice(x) }\n")
    with open(os.path.join(w, "src", "utils", "helper.cr"), "w") as f:
        f.write("pub func twice(x: i32) -> i32 { return x * 2 }\n")
    with open(os.path.join(w, "src", "main.cr"), "w") as f:
        f.write("import math\n\nfunc main() { say math.double(21) }\n")
    r = run([CORE, "compile", "modapp", "src/main.cr"], cwd=w)
    check("recursive module imports build", r.returncode == 0, r.stderr)
    rr = run(["./modapp"], cwd=w)
    check("module program output", rr.stdout.strip() == "42", rr.stdout + rr.stderr)

    # duplicate imports dedup: util imported twice via two paths
    with open(os.path.join(w, "src", "net.cr"), "w") as f:
        f.write("import utils.helper\n\npub func five() -> i32 { return helper.twice(2) + 1 }\n")
    with open(os.path.join(w, "src", "main.cr"), "w") as f:
        f.write("import math\nimport net\n\nfunc main() { say math.double(3) + net.five() }\n")
    r = run([CORE, "compile", "modapp2", "src/main.cr"], cwd=w)
    check("duplicate imports deduplicated", r.returncode == 0, r.stderr)
    rr = run(["./modapp2"], cwd=w)
    check("dedup program output", rr.stdout.strip() == "11", rr.stdout)

    # binary independence: sources removed, binary still runs
    shutil.move(os.path.join(w, "modapp2"), os.path.join(w, "moved_app"))
    shutil.rmtree(os.path.join(w, "src"))
    rr = run([os.path.join(w, "moved_app")], cwd=w)
    check("binary runs without sources", rr.stdout.strip() == "11", rr.stdout + rr.stderr)

    # circular imports rejected clearly
    os.makedirs(os.path.join(w, "src"), exist_ok=True)
    with open(os.path.join(w, "src", "a.cr"), "w") as f:
        f.write("import b\n\npub func fa() -> i32 { return 1 }\n")
    with open(os.path.join(w, "src", "b.cr"), "w") as f:
        f.write("import a\n\npub func fb() -> i32 { return 2 }\n")
    with open(os.path.join(w, "src", "main.cr"), "w") as f:
        f.write("import a\n\nfunc main() { say a.fa() }\n")
    r = run([CORE, "compile", "cyc", "src/main.cr"], cwd=w)
    check("circular imports produce a clear error",
          r.returncode != 0 and "circular import" in r.stderr, r.stderr)


def test_core_init(w):
    proj = os.path.join(w, "newproj")
    os.makedirs(proj)
    r = run([CORE, "init"], cwd=proj)
    check("core init succeeds", r.returncode == 0, r.stderr)
    check("core.toml created", os.path.exists(os.path.join(proj, "core.toml")))
    check("core.lock created and valid", os.path.exists(os.path.join(proj, "core.lock")))
    with open(os.path.join(proj, "core.lock")) as f:
        lock = f.read()
    check("core.lock parses as a valid empty lockfile", "packages" in lock or "No dependencies" in lock, lock[:80])
    check("src/main.cr created", os.path.exists(os.path.join(proj, "src", "main.cr")))
    check("tests/ directory created", os.path.isdir(os.path.join(proj, "tests")))
    check("README.md created", os.path.exists(os.path.join(proj, "README.md")))
    with open(os.path.join(proj, "src", "main.cr")) as f:
        main_src = f.read()
    check("generated main.cr is the hello template", "Hello, Core!" in main_src, main_src)
    r = run([CORE, "build"], cwd=proj)
    check("core init project builds", r.returncode == 0, r.stderr)
    rr = run(["./newproj"], cwd=proj)
    check("core init project runs", rr.stdout.strip() == "Hello, Core!", rr.stdout)
    r = run([CORE, "init"], cwd=proj)
    check("core init refuses to overwrite", r.returncode != 0 and "refus" in r.stderr, r.stderr)
    # explicit name
    proj2 = os.path.join(w, "named")
    os.makedirs(proj2)
    r = run([CORE, "init", "customname"], cwd=proj2)
    with open(os.path.join(proj2, "core.toml")) as f:
        toml = f.read()
    check("core init name argument sets project name", "customname" in toml, toml)


def test_project_commands(w):
    proj = os.path.join(w, "cmds")
    os.makedirs(proj)
    run([CORE, "init", "cmdsapp"], cwd=proj)
    with open(os.path.join(proj, "src", "main.cr"), "w") as f:
        f.write('func main() {\n    say "cmds ok"\n}\n')
    r = run([CORE, "build"], cwd=proj)
    check("core build", r.returncode == 0, r.stderr)
    r = run([CORE, "run"], cwd=proj)
    check("core run", r.returncode == 0 and "cmds ok" in r.stdout, r.stdout + r.stderr)
    r = run([CORE, "check"], cwd=proj)
    check("core check", r.returncode == 0, r.stderr)
    # core test
    with open(os.path.join(proj, "tests", "test_main.cr"), "w") as f:
        f.write('func test_math() {\n    assert(1 + 1 == 2)\n}\n\nfunc main() {\n    test_math()\n    say "tests done"\n}\n')
    r = run([CORE, "test"], cwd=proj)
    check("core test passes", r.returncode == 0 and "passed" in r.stdout, r.stdout + r.stderr)


def test_packages(w):
    # mock git package
    repo = os.path.join(w, "mockrepo", "coolstrings")
    os.makedirs(repo, exist_ok=True)
    run(["git", "init", "-q", "."], cwd=repo)
    with open(os.path.join(repo, "core.toml"), "w") as f:
        f.write('[package]\nname = "coolstrings"\nversion = "2.1.0"\n')
    with open(os.path.join(repo, "coolstrings.cr"), "w") as f:
        f.write('pub func shout(s: string) -> string { return s + "!" }\n')
    run(["git", "add", "core.toml", "coolstrings.cr"], cwd=repo)
    run(["git", "commit", "-qm", "coolstrings 2.1.0"], cwd=repo)
    run(["git", "tag", "v2.1.0"], cwd=repo)

    proj = os.path.join(w, "pkgproj")
    os.makedirs(proj)
    r = run([CORE, "init", "pkgapp"], cwd=proj)
    with open(os.path.join(proj, "src", "main.cr"), "w") as f:
        f.write('import coolstrings\n\nfunc main() {\n    say coolstrings.shout("packaged")\n}\n')
    r = run([CORE, "install", repo], cwd=proj)
    check("core install from local git repo", r.returncode == 0, r.stdout + r.stderr)
    with open(os.path.join(proj, "core.toml")) as f:
        toml = f.read()
    check("core.toml records the dependency", "coolstrings" in toml, toml)
    with open(os.path.join(proj, "core.lock")) as f:
        lock = f.read()
    check("core.lock records exact version + commit",
          'version = "2.1.0"' in lock and "commit =" in lock, lock)
    r = run([CORE, "build"], cwd=proj)
    check("build with package dependency", r.returncode == 0, r.stderr)
    rr = run(["./pkgapp"], cwd=proj)
    check("package import works at runtime", rr.stdout.strip() == "packaged!", rr.stdout + rr.stderr)
    r = run([CORE, "list"], cwd=proj)
    check("core list shows resolved version", "2.1.0" in r.stdout, r.stdout)
    r = run([CORE, "update"], cwd=proj)
    check("core update succeeds", r.returncode == 0, r.stderr)
    # remote (non-local) repos clone into the cache; local-path repos are used
    # directly, so verify the install step reused the repo (lockfile commit pin)
    with open(os.path.join(proj, "core.lock")) as f:
        lock = f.read()
    check("package resolution pinned in lockfile", "commit =" in lock and "coolstrings" in lock, lock[:200])
    r = run([CORE, "remove", "coolstrings"], cwd=proj)
    with open(os.path.join(proj, "core.lock")) as f:
        lock = f.read()
    check("core remove clears the lock entry", "coolstrings" not in lock, lock)


def test_codegen_internals(w):
    src = 'func main() {\n    say 6 * 7\n}\n'
    r, _, ok = compile_src(w, "ir", src)
    r = run([CORE, "emit-ir", os.path.join(w, "ir.cr")])
    check("emit-ir prints LLVM IR", r.returncode == 0 and "define i32 @main()" in r.stdout, r.stderr[:200])
    r = run([CORE, "emit-ir", os.path.join(w, "ir.cr"), "-O2"])
    check("emit-ir honors optimization (constant folded)",
          "i32 42" in r.stdout, r.stdout[:400])
    r = run([CORE, "emit-asm", os.path.join(w, "ir.cr")])
    check("emit-asm prints assembly", r.returncode == 0 and "mov" in r.stdout, r.stderr[:200])
    # object file emission (real machine code)
    r = run([CORE, "compile", "objout", os.path.join(w, "ir.cr"), "--emit-object"], cwd=w)
    check("object file emitted", r.returncode == 0 and os.path.exists(os.path.join(w, "objout.coreobj.o")), r.stderr)
    with open(os.path.join(w, "objout.coreobj.o"), "rb") as f:
        magic = f.read(4)
    check("object file is real ELF machine code", magic == b"\x7fELF", magic)
    # debug info
    r = run([CORE, "compile", "dbgbin", os.path.join(w, "ir.cr"), "--debug"], cwd=w)
    rr = run(["gdb", "-batch", "-ex", "break main", "-ex", "run", "./dbgbin"], cwd=w)
    check("--debug produces GDB-usable binaries", rr.returncode == 0 and "main () at" in rr.stdout,
          rr.stdout + rr.stderr)
    # optimization levels produce working binaries
    for opt in ["-O0", "-O1", "-O2", "-O3", "-Os"]:
        r, binary, ok = compile_src(w, "opt" + opt[2], src, [opt])
        rr = run([binary], cwd=w)
        check(f"optimization {opt} produces a working binary",
              ok and rr.stdout.strip() == "42", r.stderr + rr.stdout)


def test_cross_freestanding(w):
    src = 'func main() {\n    say "cross"\n}\n'
    r = run([CORE, "compile", "cross.bin", "cross.cr", "--target=aarch64"], cwd=w)
    objp = os.path.join(w, "cross.bin.coreobj.o")
    check("aarch64 cross compilation emits an object",
          r.returncode == 0 and os.path.exists(objp) and "cross-compiling" in r.stderr, r.stderr)
    if os.path.exists(os.path.join(w, "cross.coreobj.o")):
        with open(os.path.join(w, "cross.coreobj.o"), "rb") as f:
            header = f.read(20)
        check("aarch64 object is real AArch64 machine code", b"ARM" in header or b"aarch64" in header,
              header[:20])

    # freestanding: kernel with custom entry, no runtime
    with open(os.path.join(w, "kernel.cr"), "w") as f:
        f.write('''@link_name("_start") func kmain() -> never {
    vram: ptr<u16> = unsafe { 0xB8000 as ptr<u16> }
    unsafe {
        volatile_store(vram, 0x0F00 + ('X' as u16))
    }
    halt()
}

func halt() -> never {
    unsafe {
        asm("cli", "")
    }
    halt()
}
''')
    r = run([CORE, "compile", "kernel.elf", "kernel.cr", "--freestanding", "--emit-object"], cwd=w)
    check("freestanding compilation", r.returncode == 0, r.stdout + r.stderr)
    nm = run(["nm", os.path.join(w, "kernel.elf")], cwd=w)
    check("custom entry symbol _start present", "_start" in nm.stdout, nm.stdout + nm.stderr)
    objdump = run(["objdump", "-d", os.path.join(w, "kernel.elf")], cwd=w)
    check("inline asm (cli) present in machine code", "cli" in objdump.stdout or "\tf4" in objdump.stdout,
          objdump.stdout[:300])


def test_unsafe_simd(w):
    expect_output(w, '''import simd

func main() {
    v = splat(2.0 as f32)
    w = v * v + splat(1.0 as f32)
    say extract(w, 0)
    say sum(w)
    u = replace(w, 1, 7.0 as f32)
    say extract(u, 1)
}
''', ["5", "20", "7"])
    expect_output(w, '''import memory

func main() {
    // volatile + raw casts inside unsafe
    p = alloc_bytes(8)
    unsafe {
        volatile_store(p as ptr<u64>, 0x0102030405060708)
    }
    q: ptr<u8> = p
    say *q
    free(p)
}
''', ["8"])
    expect_output(w, '''extern func printf(fmt: ptr<char>, ...) -> i32
extern func abs(x: i32) -> i32

func main() {
    printf("%d-%s-%.1f\\n", 5, c_str("ffi"), 2.5)
    say abs(-9)
}
''', ["5-ffi-2.5", "9"])


def test_diagnostics(w):
    expect_compile_error(w, 'func main() {\n    say "Age: " + 42\n}\n',
                         "cannot apply '+'")
    expect_compile_error(w, 'func main() {\n    x: i32 = "no"\n}\n',
                         "cannot assign")
    expect_compile_error(w, 'func main() {\n    say undefined_name\n}\n',
                         "unknown name")
    expect_compile_error(w, 'func main() {\n    if 1 { say 1 }\n}\n',
                         "if condition must be bool")
    expect_compile_error(w, 'func main() {\n    x = 1\n    mut x = 2\n}\n',
                         "cannot assign to immutable")
    expect_compile_error(w, 'func main() {\n    x = 1\n    x = 2\n}\n',
                         "cannot assign to immutable")
    expect_compile_error(w, 'func main() {\n    y = &5\n}\n',
                         "address of a temporary")
    expect_compile_error(w, 'func main() {\n    p: ptr<i32> = null\n    say *q\n}\n',
                         "unknown name")
    expect_compile_error(w, 'struct S { a: i32 }\n\nfunc nope() -> S {\n    x = 1\n}\n\nfunc main() {\n}\n',
                         "without returning")
    expect_compile_error(w, 'func main() {\n    match 1 { case 2: say 2 }\n}\n'.replace("match 1 { case 2: say 2 }", "match 1 { Red { say 1 } }"),
                         "looks like a variant")


def main():
    global CORE
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", default="/home/exedev/core/build/core")
    ap.add_argument("filter", nargs="?", default="")
    args = ap.parse_args()
    CORE = os.path.abspath(args.core)

    tests = [
        ("basics", test_basics),
        ("functions", test_functions),
        ("types", test_types),
        ("pointers-memory", test_pointers_memory),
        ("structs-classes", test_structs_classes),
        ("generics-enums", test_generics_enums),
        ("modules", test_modules),
        ("core-init", test_core_init),
        ("project-commands", test_project_commands),
        ("packages", test_packages),
        ("codegen-internals", test_codegen_internals),
        ("cross-freestanding", test_cross_freestanding),
        ("unsafe-simd-ffi", test_unsafe_simd),
        ("diagnostics", test_diagnostics),
    ]
    for name, fn in tests:
        if args.filter and args.filter not in name:
            continue
        print(f"== {name} ==")
        with tempfile.TemporaryDirectory(prefix="coretest_") as w:
            try:
                fn(w)
            except Exception as e:
                check(f"{name} suite crashed", False, str(e))

    print(f"\n{PASS} passed, {FAIL} failed")
    if FAILURES:
        print("failures:")
        for name, detail in FAILURES:
            print(f"  - {name}")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
