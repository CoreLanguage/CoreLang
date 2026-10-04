# Core Documentation

## The guide (start here)

Reference documentation for working programmers, in docs/guide/:

- [guide/installation.md](guide/installation.md) - one-line install, or build from source
- [guide/getting-started.md](guide/getting-started.md) - first project, core init/build/run, what the compiler does
- [guide/syntax.md](guide/syntax.md) - the whole grammar in one page: declarations, statements, operators, precedence
- [guide/types.md](guide/types.md) - every primitive, literals, casts, the no-implicit-conversion rule
- [guide/functions.md](guide/functions.md) - parameters, overloading, defaults, generics, lambdas, extern
- [guide/control-flow.md](guide/control-flow.md) - if/while/for/loop/switch/match
- [guide/memory.md](guide/memory.md) - pointers, heap allocation, undefined behavior
- [guide/structs-and-enums.md](guide/structs-and-enums.md) - structs, methods, enums with data
- [guide/oop.md](guide/oop.md) - classes, inheritance, vtables, interfaces, traits
- [guide/generics.md](guide/generics.md) - monomorphization, inference, no bounds
- [guide/modules-and-packages.md](guide/modules-and-packages.md) - imports, pub, core.toml, packages
- [guide/stdlib.md](guide/stdlib.md) - prelude, memory, math, thread, time, process, simd
- [guide/unsafe-and-low-level.md](guide/unsafe-and-low-level.md) - unsafe, volatile/MMIO, inline asm, FFI, freestanding
- [guide/command-line.md](guide/command-line.md) - every subcommand and flag

## The precise rules (normative)

- [language/SPEC.md](language/SPEC.md) - the normative language specification
- [language/memory-model.md](language/memory-model.md) - stack/heap/static, ownership, UB catalog
- [language/abi.md](language/abi.md) - calling conventions, layouts, name mangling
- [language/concurrency.md](language/concurrency.md) - threads, atomics, memory visibility

## Standard library and packages

- [stdlib/overview.md](stdlib/overview.md) - the stdlib contract, module by module
- [packages/overview.md](packages/overview.md) - the git-based ecosystem
- [packages/manifest.md](packages/manifest.md) - core.toml reference
- [packages/lockfile.md](packages/lockfile.md) - core.lock and reproducibility
- [packages/publishing.md](packages/publishing.md) - authoring, versioning, publishing

## Contributing to the compiler

- [contributing/getting-started.md](contributing/getting-started.md) - build, test, conventions
- [contributing/adding-a-feature.md](contributing/adding-a-feature.md) - recipes for every kind of change
- [contributing/testing.md](contributing/testing.md), [contributing/release-checklist.md](contributing/release-checklist.md)
- [compiler/architecture.md](compiler/architecture.md) - pipeline and directory map; subsystem guides in [compiler/](compiler/)
- [internals/monomorphization.md](internals/monomorphization.md) - generics end-to-end
- [internals/abi.md](internals/abi.md) - vtable/itable/closure/mangling details

## Ground truth

- [LANGUAGE-REFERENCE-SUMMARY.md](LANGUAGE-REFERENCE-SUMMARY.md) - a one-page summary of exactly what the compiler implements today. Documentation must stay in sync with it.
