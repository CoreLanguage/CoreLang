# Getting started

Core is a compiled, statically typed systems language. It has manual
memory management, raw pointers, and no garbage collector. Programs
compile to a single native executable; there is no VM and no runtime
dependency beyond libc and pthreads.

Install Core first. The quick way:

```console
curl -fsSL https://raw.githubusercontent.com/snitchbossdotcom/corelang/main/install.sh | bash
```

See [installation.md](installation.md) for what that does and for the
build-from-source route. Check that it worked:

```console
$ core version
Core compiler 0.1.0 (LLVM 18.1.x backend)
```

## Your first program

Scaffold a project:

```console
$ core init hello
$ cd hello
```

`core init` creates this layout:

```
hello/
  core.toml      project manifest (name, version, dependencies)
  core.lock      pinned dependency versions
  src/main.cr    entry point
  tests/         test files (each is its own program with main())
```

`src/main.cr` already contains a working program:

```core
func main() {
    say "Hello, Core!"
}
```

`say` prints a value and a newline. It is overloaded for every
primitive type, so `say 42` and `say 1.5` work without any imports.

Build and run it:

```console
$ core run
Hello, Core!
```

The compiled binary is named after the package in `core.toml` (here,
`hello`). It is self-contained: you can copy it to another machine of
the same architecture and it runs, no sources or libraries needed.

## What the compiler actually does

`core` is a whole-program compiler. For every build it:

1. Parses every `.cr` file your program imports, starting from the
   entry file, into one module each.
2. Resolves names and checks types. Generic functions and types are
   monomorphized: one clone per concrete type used.
3. Generates LLVM IR, optimizes it at the selected level, and emits a
   native object file.
4. Invokes the system linker (`cc`) with the small Core runtime
   (`corert.o`) and libc. The result is one executable.

There is no incremental compilation and no object cache. For the
program sizes Core targets today, that is fine, and it keeps the
compiler simple.

## Everyday workflow

```console
$ core build        # build from core.toml, output named after the package
$ core run          # build, then run
$ core run arg1     # build, then run with program arguments
$ core check src/main.cr   # type-check only, no code generation
$ core test         # compile and run every tests/*.cr file
```

Single-file programs work too, without a project:

```console
$ core compile hello main.cr
$ ./hello
```

`compile` refuses to overwrite an existing output unless you pass
`--force`; `build` and `run` own their artifacts and overwrite freely.

## A slightly bigger example

```core
import memory

struct Point {
    x: i32
    y: i32
    pub func sum() -> i32 { return self.x + self.y }
}

func main() {
    mut ps: [Point; 3]
    for i in 0..3 {
        ps[i] = Point { x: i, y: i * 2 }
    }
    mut total: i32 = 0
    for p in ps { total += p.sum() }
    say total          // 18

    // heap allocation is explicit and manual
    p = alloc<Point>()
    p.x = 3
    p.y = 4
    say p.sum()        // 7
    free(p)
}
```

If `import memory` looks strange: the standard library is ordinary Core
code split into modules; only the small prelude (printing, `assert`,
`panic`, `Option`, `Result`) is always available. See
[stdlib.md](stdlib.md).

## Where to go next

- [syntax.md](syntax.md) - the full grammar in one page
- [types.md](types.md) - every type and the conversion rules
- [memory.md](memory.md) - pointers, allocation, and what is unsafe
- [command-line.md](command-line.md) - every subcommand and flag
- [language/SPEC.md](../language/SPEC.md) - the normative rules when you
  need exact answers
