# FFI: calling C

Core speaks the **C ABI natively**: declare a C function with `extern func`, and it's a direct call — no wrappers, no marshalling layer. This makes libc, libm, and every C library usable immediately.

## extern declarations

```core
extern func printf(fmt: ptr<char>, ...) -> i32   // variadic
extern func strlen(s: ptr<char>) -> i32
extern func pow(x: f64, y: f64) -> f64           // from libm
```

- `extern func` = "this symbol exists at link time". No body, **no name mangling**.
- Types map 1:1 to C: `i32`=int, `i64`=long long, `f64`=double, `char`=char, `ptr<T>`=`T*`, `ptr<void>`=`void*`.
- Declare them at top level, use them like any function.

## Variadic C functions

`...` marks the variadic tail. Variadic arguments must be **integers, floats, pointers, or bools** — they're passed per the C ABI's promotion rules:

```core
extern func printf(fmt: ptr<char>, ...) -> i32

func main() {
    printf("hello from libc: %d %s %.2f\n", 7, c_str("strings cross the ABI"), 1.5)
    printf("%s|mix %d %c\n", c_str("str"), 9, 'x')
    // printf("implicit %s\n", "again")   // ERROR: string is a view, not a pointer
}
```

**The string rule**: a Core `string` is a 16-byte `{ptr, len}` view — it cannot cross into a variadic call. Use `c_str(s)` to get the `ptr<char>` C expects.

## Strings across the boundary

| Direction | Function |
|---|---|
| Core → C (`%s`, `char*` params) | `c_str(s)` — pointer to the view's bytes |
| C → Core (`char*` return) | `str_from_c(p)` — wraps a NUL-terminated buffer in a view |

```core
extern func printf(fmt: ptr<char>, ...) -> i32
extern func getenv(name: ptr<char>) -> ptr<char>

func main() {
    home = getenv("HOME")
    if home == null {
        printf("no HOME\n")
    } else {
        say str_from_c(home) != ""    // true — wrapped as a view
    }
}
```

Non-variadic extern calls accept a `string` **implicitly** where `ptr<char>` is expected (`strlen("implicit?")` works) — the view's pointer goes over. For variadic calls it must be explicit: `c_str(...)`.

Lifetimes: `c_str` of a literal is valid forever; of a concatenated string, as long as that buffer lives (the runtime owns it; there's no per-string free).

## Structs and pointers in FFI

Core structs with C-compatible layout pass through the ABI directly — declare the matching struct and pass pointers:

```core
// struct timespec { long tv_sec; long tv_nsec; }
struct Timespec {
    tv_sec: i64
    tv_nsec: i64
}

extern func nanosleep(req: ptr<Timespec>, rem: ptr<Timespec>) -> i32

func sleep_ns(ns: i64) {
    req = Timespec { tv_sec: ns / 1000000000, tv_nsec: ns % 1000000000 }
    rem: ptr<Timespec> = null     // typed nulls pass where ptr<T> is expected
    nanosleep(&req, rem)
}
```

This is how you reach the whole POSIX world: file IO (`open/read/write`), sockets, SDL, raylib, SQLite — declare the signatures you use, link the library, go.

## Linking native libraries

Declarations don't link code — the final `cc` invocation does. Options:

1. **Default link line**: every program links `-lm -lpthread -ldl -latomic` already (plus libc) — `pow`, `pthread_*`, `nanosleep` work out of the box.
2. **`--link=<lib>`**: appends `-l<lib>`:
   ```bash
   core compile myapp src/main.cr --link=sdl2 --link=SDL2_image
   ```
3. **`--lib-path=<dir>`**: adds a library search path (`-L<dir>`).
4. **`--link-arg=<arg>`**: raw linker flags for anything else (`-Wl,-rpath,...`).
5. **`core.toml [build]`** for project-wide defaults (picked up by `core build`):
   ```toml
   [build]
   link = ["sdl2"]
   link-paths = ["/opt/sdl/lib"]
   link-args = ["-Wl,-rpath,/opt/sdl/lib"]
   ```
   Dependencies can contribute their own `link` entries; they merge.

## Complete working example

```core
// ffi.cr
extern func printf(fmt: ptr<char>, ...) -> i32
extern func pow(x: f64, y: f64) -> f64
extern func strlen(s: ptr<char>) -> i32
extern func getenv(name: ptr<char>) -> ptr<char>

func main() {
    printf("2^10 = %.0f\n", pow(2.0, 10.0))        // 1024
    say strlen("implicit?")                        // 9 — implicit string->ptr<char>
    say strlen(c_str("explicit"))                  // 8

    home = getenv("HOME")
    if home == null {
        printf("no HOME\n")
    } else {
        say str_from_c(home) != ""                 // true
    }
    printf("%s|mix %d %c\n", c_str("str"), 9, 'x')
}
```

Verified output: `2^10 = 1024`, `9`, `8`, `true`, `str|mix 9 x`.

## Common mistakes

- **Passing a bare `string` to variadic C calls.** Rejected at compile time; use `c_str(s)`.
- **Wrong prototypes.** A wrong `extern` signature (int vs pointer, missing variadic) compiles fine and explodes at runtime — double-check against the C header.
- **Forgetting the library.** `undefined reference to 'SDL_Init'` means the declaration exists but the lib isn't linked — add `--link` or the `[build] link` entry.
- **Holding C pointers across C calls that invalidate them** (e.g. `getenv` results after `setenv`) — C's rules apply.
- **Assuming NUL termination.** Core strings aren't NUL-terminated; only `c_str` results passed *to* C are (the runtime manages it). Don't scan past `len`.

## Performance notes

- `extern` calls are direct machine calls — same cost as from C, no translation layer.
- Small hot libc calls (strlen, memcmp) are inlined/expanded by LLVM at `-O2` when it recognizes them.
- Crossing into C disables Core-side optimization across the call — batch work (pass arrays/slices) instead of calling per-element.

## When to use / not use

- **Use FFI** for: OS APIs not in Core's std (files, sockets, time — `std/time` and `std/process` are thin externs themselves), existing C libraries, benchmarking against C.
- **Wrap, don't litter**: centralize extern declarations in one module (e.g. `c_api.cr`) with clean Core-style wrappers — see [modules.md](modules.md).
- **Don't** call variadic functions in hot loops (va_list parsing isn't free) — prefer non-variadic entry points when the library has them.
- For assembler-level control, skip C entirely: [inline-assembly.md](inline-assembly.md).

## Exercises

1. Declare and call `strlen`, `strcmp`, and `abs` from libc; print the results for three inputs.
2. Wrap `getenv("PATH")` into a safe Core function returning `Option<string>` — `null` maps to `None`.
3. Declare `nanosleep` with its `Timespec` struct and write `sleep_us(us: i64)`; time it with `time.monotonic_ms()`.
4. Use `pow` and `sqrt` from libm (already linked) to compute the hypotenuse of a 3-4-5 triangle through C calls.
5. Put all your extern declarations into a `c_api.cr` module and re-export friendly wrappers (see [modules.md](modules.md) for visibility rules).

Next: [Unsafe](unsafe.md).
