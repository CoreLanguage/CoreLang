# Standard library

The standard library is ordinary Core code in `std/*.cr`, shipped with
the compiler. It is small on purpose: no collections framework, no
string builder, no async. What is here is thin, predictable, and does
what it says.

## Prelude (auto-imported)

Loaded into every program before anything else. No import needed.

| Name | What it is |
|------|------------|
| `say(v)` | print any primitive or `ptr<T>`, newline; also `say expr` sugar |
| `print(v)` | print without newline (string, i32, i64, u64) |
| `to_string(v)` | text conversion for every numeric type, bool, char; allocates |
| `assert(cond)` / `assert(cond, msg)` | abort with file and line on failure |
| `panic(msg) -> never` | abort with a message |
| `c_str(s)` | `ptr<char>` of a string view, for FFI |
| `str_from_c(p)` | string view over a NUL-terminated C string |
| `str_eq(a, b)` / `str_cmp(a, b)` | byte comparison helpers |
| `Option<T>`, `Result<T, E>` | the two standard generic enums |
| `to_string` overloads | every numeric type, bool, char |

`Option<T>` is `Some(T)` / `None`; `Result<T, E>` is `Ok(T)` / `Err(E)`.
Both are plain data-carrying enums, matched with `match`
([control-flow.md](control-flow.md)).

```core
o: Option<i32> = Option<i32>.Some(3)
match o {
    Some(v) { say v }
    None    { say "missing" }
}
```

## memory

Manual heap management. Every allocation needs exactly one `free`.
Documented fully in [memory.md](memory.md); the functions are
`alloc<T>`, `alloc_zeroed<T>`, `alloc_array<T>`, `alloc_zeroed_array<T>`,
`alloc_bytes`, `alloc_aligned`, `realloc_array<T>`, `free<T>`,
`memcpy`, `memset`, `memcmp`.

```core
import memory

p = alloc_zeroed<i64>()
p[0] = 7          // p[i] is *(p + i)
free(p)
```

## math

```core
import math

PI, E                                  // f64 constants
sqrt, pow, sin, cos, abs, floor, ceil  // f64 math
abs(x: i32), abs(x: i64)               // integer overloads
min<T>, max<T>, clamp<T>               // any orderable T
```

```core
import math
say math.sqrt(2.0)
say math.clamp(15, 0, 10)      // 10
```

## thread

POSIX threads with no scheduler and no hidden state. Threads and locks
are what they look like; nothing coordinates with a runtime because
there is no runtime layer above pthreads.

| Item | What it is |
|------|------------|
| `spawn(f: func()) -> ptr<void>` | run a closure on a new OS thread; returns an opaque handle |
| `join(t)` | block until the thread finishes |
| `Mutex` | `init`, `lock`, `unlock`, `deinit` |
| `RwLock` | `read`, `write`, `unlock`, `deinit` |
| `Cond` | `wait(m: Mutex)`, `signal`, `broadcast`, `deinit` |
| `AtomicI32 / AtomicI64 / AtomicBool / AtomicUsize` | load/store/add/sub/swap/compare_exchange, sequentially consistent |

```core
import thread

func main() {
    mut done = false
    p = &done
    t = thread.spawn(func() { *p = true })
    thread.join(t)
    say done
}
```

Closures capture by value, so share state through a pointer, and lock
what multiple threads touch (`Mutex` in the example above; the full
pattern is in examples/threads). Mutex and Cond require an explicit
`deinit()`; there is no destructor to forget.

Memory visibility for unsynchronized access is undefined; use the
atomics or a lock. Details: [language/concurrency.md](../language/concurrency.md).

## time

```core
import time
time.time_ms()        // milliseconds since the Unix epoch (u64)
time.monotonic_ms()   // for measuring durations
time.sleep_ms(ms)     // block the calling thread
```

## process

```core
import process
process.exit(1)             // -> never, terminates immediately
process.arg_count()         // including the program name
process.arg(0)              // program name; arg(1) is the first argument
```

Arguments come from `/proc/self/cmdline` on Linux. `arg(i)` returns an
empty string when out of range.

## simd

Vector types (`f32x4`, `i32x4`, ...) are primitives, and `+ - * /`
between equal vector types are lane-wise LLVM vector operations. The
`simd` module adds the rest:

| Function | What it does |
|----------|--------------|
| `splat(v)` | broadcast a scalar into every lane (one overload per vector type) |
| `extract(v, i)` | read lane `i` |
| `replace(v, i, x)` | copy with lane `i` replaced |
| `sum(v)` | horizontal sum of all lanes (f32x4) |

```core
import simd

func dot(a: f32x4, b: f32x4) -> f32 {
    return simd.sum(a * b)     // lane-wise multiply, then reduce
}
```

## What is deliberately absent

No collections beyond arrays and what you build on `memory` (the
`DynArray` pattern in [generics.md](generics.md)), no hashmap, no
regex, no JSON, no path handling, no IO. Core programs do IO through
the C library (`extern func printf`, `open`, `read`, ...) which is
exactly what [unsafe-and-low-level.md](unsafe-and-low-level.md)
documents. The stdlib grows by packages, not by baking everything into
the compiler.
