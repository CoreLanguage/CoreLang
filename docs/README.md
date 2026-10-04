# Core Documentation

Welcome! Here is where to start, by goal:

## "I want to learn Core"
- [guide/getting-started.md](guide/getting-started.md) — install, hello world, the toolchain
- [guide/syntax.md](guide/syntax.md) — the whole grammar in one page
- [guide/types.md](guide/types.md) — every type, literals, conversion rules
- [guide/functions.md](guide/functions.md) — params, returns, overloads, generics, closures
- [guide/control-flow.md](guide/control-flow.md) — if/while/for/match
- [guide/memory.md](guide/memory.md) — pointers, alloc/free, unsafe, what is UB
- [guide/structs-and-enums.md](guide/structs-and-enums.md) — value types, methods, tagged unions
- [guide/oop.md](guide/oop.md) — classes, inheritance, interfaces, traits
- [guide/generics.md](guide/generics.md) — monomorphization and type inference
- [guide/modules-and-packages.md](guide/modules-and-packages.md) — imports, visibility, core.toml
- [guide/stdlib.md](guide/stdlib.md) — what the standard library actually contains
- [guide/unsafe-and-low-level.md](guide/unsafe-and-low-level.md) — unsafe, volatile/MMIO, inline asm, FFI
- [guide/command-line.md](guide/command-line.md) — every `core` subcommand and flag

## "I want the precise rules"
- [language/SPEC.md](language/SPEC.md) — the normative language specification
- [language/memory-model.md](language/memory-model.md) — stack/heap/static, ownership, UB catalog
- [language/abi.md](language/abi.md) — calling conventions, layouts, name mangling
- [language/concurrency.md](language/concurrency.md) — threads, atomics, memory visibility

## "I want to hack on the compiler"
- [compiler/architecture.md](compiler/architecture.md) — pipeline and directory map
- [compiler/](compiler/) subsystem guides: lexer, parser, ast, semantic,
  type-system, codegen, driver, debugging, diagnostics
- [internals/monomorphization.md](internals/monomorphization.md) — generics end-to-end
- [internals/abi.md](internals/abi.md) — the vtable/itable/closure/mangling details
- [contributing/getting-started.md](contributing/getting-started.md) — build, test, conventions
- [contributing/adding-a-feature.md](contributing/adding-a-feature.md) — recipes for every
  kind of change (keyword, operator, type, optimization, CLI command, ...)
- [contributing/testing.md](contributing/testing.md), [contributing/release-checklist.md](contributing/release-checklist.md)

## "I want to publish or use a package"
- [packages/overview.md](packages/overview.md) — the git-based ecosystem
- [packages/manifest.md](packages/manifest.md) — core.toml reference
- [packages/lockfile.md](packages/lockfile.md) — core.lock and reproducibility
- [packages/publishing.md](packages/publishing.md) — authoring, versioning, publishing

## "I want to know what the standard library gives me"
- [stdlib/overview.md](stdlib/overview.md) — the small-but-honest stdlib contract

## Ground truth
- [LANGUAGE-REFERENCE-SUMMARY.md](LANGUAGE-REFERENCE-SUMMARY.md) — a one-page summary of
  exactly what the compiler implements today. Documentation must stay in sync with it.
