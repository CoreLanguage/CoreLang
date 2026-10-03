# The Core Standard Library

The standard library ships with the compiler and lives in `std/` next to
it (found at runtime relative to the `core` binary, overridable with
`CORE_HOME`). It is deliberately **small**: seven modules, a few hundred
lines total.

---

## 1. The contract

Every stdlib module obeys three rules:

1. **Small.** The stdlib is a thin, readable layer over libc and the
   runtime. If a feature needs a real implementation (JSON, regex,
   HTTP), it is a package, not a stdlib module.
2. **No GC — there is no garbage collector to hide behind.** Nothing in
   the stdlib creates hidden ownership, finalizers, or cleanup threads.
   What you allocate, you free.
3. **No hidden allocation** beyond the documented exceptions:
   - `string + string` allocates the joined buffer (SPEC.md §5.5),
   - `thread.spawn` allocates a thread handle and a trampoline block
     (`join` frees both),
   - nothing else. Printing, math, time, atomics, and locks allocate
     nothing (locks malloc their pthread objects once, at `init`).

The prelude is **auto-imported** into every module; the other modules
require `import`. All stdlib declarations used across modules are `pub`.

Modules are ordinary Core files — you can read them (`std/prelude.cr`,
`std/memory.cr`, …), and every extern they call is a real
`runtime/corert.c` symbol you can find in the runtime source.

---

## 2. Module by module

### 2.1 prelude (auto-imported)

Printing, string interop, failures, and the two idiomatic enums.

```core
say 42                 // newline-terminated print; overloads for every
say "text"             //   primitive type, plus say<T>(v: ptr<T>)
print("no newline")    // string / i32 / i64 / u64 overloads only

assert(x > 0)                          // file:line on failure, aborts
assert(x > 0, "x must be positive")
panic("unreachable")                   // -> never; aborts

c_str(s)                               // ptr<char> for C calls
str_from_c(p)                          // string view over a C string
str_eq(a, b) / str_cmp(a, b)           // equality / three-way compare

o = Option<i32>.Some(3)
r = Result<i32, string>.Err("bad")
```

`panic` and `process.exit` are the only `never` functions in the stdlib;
calling one makes following code unreachable (SPEC.md §8.5).

### 2.2 memory — manual allocation

`alloc<T>`, `alloc_zeroed<T>`, `alloc_array<T>`, `alloc_zeroed_array<T>`,
`alloc_bytes`, `alloc_aligned`, `realloc_array<T>`, `free<T>`,
`memcpy`, `memset`, `memcmp`.

Thin wrappers over malloc/calloc/realloc/free; on exhaustion they panic
(never return null). Full contracts and allocator patterns:
[../language/memory-model.md](../language/memory-model.md).

### 2.3 math — float and integer helpers

`PI`, `E` (consts); `sqrt`, `pow`, `sin`, `cos`, `abs`, `floor`, `ceil`
over `f64`; `abs` overloads for `i32`/`i64`; generic `min`, `max`,
`clamp<T>` (monomorphized per element type).

```core
import math
say math.sqrt(2.0)
say math.max<i32>(3, 9)
```

### 2.4 thread — OS threads and synchronization

`spawn(f: func()) -> ptr<void>`, `join(handle)`; `Mutex`, `RwLock`,
`Cond` classes; `AtomicI32`, `AtomicI64`, `AtomicBool`, `AtomicUsize`.
All atomic operations are sequentially consistent. Semantics, capture
rules, patterns and pitfalls:
[../language/concurrency.md](../language/concurrency.md).

### 2.5 time

`time_ms()` (milliseconds since the Unix epoch), `monotonic_ms()` (for
measuring durations), `sleep_ms(ms)`. All `u64`; thin clock_gettime /
nanosleep wrappers.

### 2.6 process

`exit(code) -> never`, `arg_count() -> i32` (includes program name),
`arg(i) -> string` (empty string when out of range). Arguments are read
from `/proc/self/cmdline` (Linux).

### 2.7 simd — vector primitives

`splat` (scalar → all lanes), `extract(v, i)`, `replace(v, i, x)` per
vector type, and `sum` (horizontal add for `f32x4`). `+ - * /` on vector
values are lane-wise and lower directly to LLVM vector IR.

```core
import simd
v = simd.splat(1.5)          // f32x4
w = v * v                    // lane-wise
say simd.sum(w)              // 9
```

---

## 3. How the stdlib differs from third-party packages

| | stdlib | third-party package |
|---|---|---|
| Source of truth | ships with the compiler (`std/`) | a git repo you pin in `core.toml`/`core.lock` |
| Importing | prelude automatic; others plain `import time` | `core install <repo>` first, then `import <module>` |
| Versioning | exactly one version — the one that ships with your compiler | semver tags, `^`/`>=`/exact constraints, lockfile pins |
| Resolution order on the import path | last (after the project and dependencies) | dependency checkouts, in manifest order |
| Upgrades | come with a compiler upgrade | `core update` |
| Guarantee | the contract in §1 (small, no GC, no hidden allocation) | whatever the package documents — audit it |
| No-GC rule | enforced by design | not enforceable by the toolchain |

Practical consequences:

- Code against the stdlib never needs dependency network access, and
  `core.lock` stays empty for stdlib-only projects.
- The stdlib is **not** namespaced or version-selected per project: a
  package that needs a different "version" of stdlib functionality
  should vendor its own module under a distinct name.
- Freestanding (`--freestanding`) drops the prelude too — bare-metal
  programs define their own entry and bring nothing in automatically.
