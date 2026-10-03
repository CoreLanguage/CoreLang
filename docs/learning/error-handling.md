# Error handling

Core has **no exceptions and no throw**. Fallible operations return a value that *describes* the outcome — `Option<T>` for "maybe absent", `Result<T, E>` for "success or failure" — and you handle it with `match`. For unrecoverable states there's `panic`; for checking invariants there's `assert`.

## The two workhorse enums (prelude)

```core
pub enum Option<T> {
    Some(T),
    None
}

pub enum Result<T, E> {
    Ok(T),
    Err(E)
}
```

They're ordinary data-carrying enums (see [enums.md](enums.md)) — no special syntax, no hidden control flow. A function returning `Result<i32, string>` either returns `Result<i32, string>.Ok(42)` or `Result<i32, string>.Err("reason")`, and the type system forces callers to acknowledge both.

## Option<T>: "might not be there"

```core
func find_index(arr: ptr<i32>, n: i32, key: i32) -> Option<i32> {
    for i in 0..n {
        if arr[i] == key { return Option<i32>.Some(i) }
    }
    return Option<i32>.None
}

func main() {
    data: [i32; 5] = [10, 20, 30, 40, 50]
    match find_index(&data, 5, 30) {
        Some(i) { say i }            // 2
        None    { say "not found" }
    }
}
```

Use `Option` instead of sentinel values (`-1`, `null`) — the "missing" case is visible in the type signature.

## Result<T, E>: "might fail"

```core
func checked_div(a: i32, b: i32) -> Result<i32, string> {
    if b == 0 { return Result<i32, string>.Err("div by zero") }
    return Result<i32, string>.Ok(a / b)
}

func main() {
    match checked_div(10, 2) {
        Ok(v)  { say v }       // 5
        Err(e) { panic(e) }    // escalate
    }
}
```

The error type is your choice — `string` is common; a custom error enum is better at scale:

```core
enum ParseErr { TooBig, NotANumber(char) }

func digit(c: char) -> Result<i32, ParseErr> {
    if c >= '0' && c <= '9' { return Result<i32, ParseErr>.Ok(c as i32 - '0' as i32) }
    return Result<i32, ParseErr>.Err(NotANumber(c))
}
```

## The match-based style

Handling is explicit and local. Chaining fallible calls means nesting or a helper — the idiomatic early-return helper:

```core
// flat pipeline via early returns
func compute(a: i32, b: i32, c: i32) -> Result<i32, string> {
    r1 = checked_div(a, b)
    match r1 {
        Err(e) { return Result<i32, string>.Err(e) }
        Ok(v)  { }    // fall through with v
    }
    // ... in v0.1, extract with a second match
    match r1 {
        Ok(v) {
            r2 = checked_div(v, c)
            match r2 {
                Ok(v2) { return Result<i32, string>.Ok(v2) }
                Err(e) { return Result<i32, string>.Err(e) }
            }
        }
        Err(e) { return Result<i32, string>.Err(e) }
    }
}
```

> There is no `?` operator in v0.1 — error propagation is by `match` + `return`. Keep error types uniform so helpers stay simple.

## panic: unrecoverable

`panic(msg) -> never` prints the message and aborts the program. It's for **bug detection**, not error handling:

```core
func get(idx: i32) -> i32 {
    if idx < 0 || idx > 10 { panic("index out of range") }
    ...
}
```

Because `panic` is `never`, code after a panic call is unreachable — the compiler knows control can't continue. Array bounds violations and failed runtime checks panic the same way (with file/line info).

## assert: checking invariants

```core
assert(cond)                // "assertion failed" + file/line on failure
assert(cond, "message")     // custom message
```

`assert` is the prelude's test and invariant tool: cheap boolean checks that abort with location info. Use it in tests (see [first-project.md](first-project.md)) and to document preconditions ("this pointer is non-null here").

## Choosing the mechanism

| Situation | Mechanism |
|---|---|
| Value may legitimately be absent | `Option<T>` |
| Operation may fail, caller should decide | `Result<T, E>` |
| Programmer error / broken invariant | `assert` (debug-style) or `panic` |
| Unrecoverable at runtime (OOM, hardware) | `panic` / `process.exit` (see [ffi.md](ffi.md) for `exit`) |
| Never exceptions | — Core has none |

## Complete working example

```core
// errors.cr - a tiny "config parser" with Result
func parse_port(s: string) -> Result<i32, string> {
    if len(s) == 0 { return Result<i32, string>.Err("empty") }
    mut n = 0
    for i in 0..len(s) {
        c = s[i]
        if c < '0' || c > '9' { return Result<i32, string>.Err("non-digit char") }
        n = n * 10 + (c as i32 - '0' as i32)
    }
    if n > 65535 { return Result<i32, string>.Err("port too big") }
    return Result<i32, string>.Ok(n)
}

func main() {
    match parse_port("8080") {
        Ok(v)  { say v }              // 8080
        Err(e) { say "bad: " + e }
    }
    match parse_port("99999") {
        Ok(v)  { say v }
        Err(e) { say "bad: " + e }    // bad: port too big
    }
    match parse_port("80x0") {
        Ok(v)  { say v }
        Err(e) { say "bad: " + e }    // bad: non-digit char
    }
    assert(is_ok_shaped(parse_port("80")))
    say ok_or_die(parse_port("443"))  // 443
    say "ok"
}

// small helper: turn a Result into a value-or-panic
func ok_or_die(r: Result<i32, string>) -> i32 {
    match r {
        Ok(v)  { return v }
        Err(e) { panic(e) }
    }
}

func is_ok_shaped(r: Result<i32, string>) -> bool {
    match r {
        Ok(v)  { return v >= 0 }
        Err(e) { return false }
    }
}
```

Verified output: `8080`, `bad: port too big`, `bad: non-digit char`, `443`, `ok`. Note `ok_or_die` — a reusable "unwrap" helper you can adapt to any error type. Also note match arms are separated by newlines (not commas).

## Common mistakes

- **Using `panic` for expected failures.** Bad input from users/files should be `Result`; panic is for bugs.
- **Ignoring the result.** `find_index(...)` without `match` just drops the Option on the floor — no compiler warning in v0.1, so build the habit.
- **Sentinel nostalgia.** Returning `-1` instead of `Option` hides failure from the type system.
- **Mixing error types.** Nesting `Result<Result<...>>` gets unwieldy — pick one `E` per API.
- **`assert` with side effects.** Treat it as free to remove; don't make the program depend on it.

## Performance notes

- `Option<T>`/`Result<T, E>` are plain tagged unions — the happy path is a tag store; zero allocation.
- `match` on them compiles to a compare + branch; LLVM often folds `match` + use into straight-line code.
- `panic`/`assert` cost a branch when not taken; asserts in hot loops are fine but measurably not free at `-O0` — at `-O2` simple asserts fold away when provably true.
- No unwind tables, no landing pads: Core binaries pay nothing for the *absence* of exceptions.

## When to use / not use

- **Use Result/Option** at API boundaries: file/string/number parsing, lookups, anything user- or IO-driven.
- **Use panic/assert** for internal invariants and "impossible" states.
- **Don't** build exception-emulation frameworks with nested enums; keep it flat and boring.
- For resource cleanup on error paths, remember: no `defer` — free before returning `Err` (see [memory-management.md](memory-management.md)).

## Exercises

1. Extend `parse_port` into `parse_int(s: string) -> Result<i32, string>` handling a leading `-`.
2. Write `unwrap_or<T>(o: Option<T>, fallback: T) -> T` with `match` and use it on a `Some` and a `None`.
3. Define `enum ParseErr { Empty, BadChar(char), TooBig }`, return it from a parser, and print a different message per variant (bind the payload).
4. Chain two fallible calls: `parse_int` then a range check, returning `Result<i32, string>` from each failure point — no exceptions, early returns only.
5. Write `tests/test_parse.cr` with three `test_*` functions using `assert` and run `core test` in a project (see [projects.md](projects.md)).

Next: [Data structures](data-structures.md).
