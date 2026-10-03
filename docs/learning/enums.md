# Enums

Core enums come in two shapes: **simple enums** (a named `i32`, like C) and **data-carrying enums** (tagged unions, like Rust). Both are matched with `match`, which is exhaustive.

## Simple enums

```core
enum Color { Red, Green, Blue }

func main() {
    c = Color.Green
    say c as i32        // 1 — i32-backed, starts at 0
}
```

- Variants are constructed with dot syntax: `Color.Red`.
- Backed by `i32`; `as i32` converts (and `as` converts back).
- Plain `==` comparison works.

## Data-carrying enums

Variants can hold values — the enum is a **tagged union**: one tag word plus enough storage for the largest payload:

```core
enum Shape {
    Circle(f64),
    Rect(f64, f64),     // two fields
    Point               // no payload
}

func area(s: Shape) -> f64 {
    return match s {
        Circle(r)  { 3.14159 * r * r }   // bind one field
        Rect(w, h) { w * h }             // bind two
        Point      { 0.0 }               // no payload
    }
}

func main() {
    say area(Shape.Circle(1.0))    // 3.14...
    say area(Shape.Rect(3.0, 4.0)) // 12
    say area(Shape.Point)          // 0
}
```

- Construct with `Shape.Circle(2.0)`, `Shape.Rect(3.0, 4.0)`, `Shape.Point`.
- `match` **extracts payloads** by binding them to names.
- Generic enums take type args at construction: `Option<i32>.Some(5)`, `Result<i32, string>.Err("boom")` — `Option<T>` and `Result<T, E>` live in the prelude (see [error-handling.md](error-handling.md)).

## match: exhaustiveness and patterns

`match` on an enum must cover **every variant** (the compiler enforces this for enums), or end with a `_` wildcard. Match arms: `Pattern { body }`. It's an expression — arms produce the match's value:

```core
func describe(c: Color) -> string {
    return match c {
        Red   { "red" }
        Green { "green" }
        Blue  { "blue" }
    }
}

// non-enum values too: literals, bindings, wildcard
func grade(n: i32) -> string {
    return match n {
        90 { "A" }
        80 { "B" }
        v  { to_string(v) + "?" }
    }
}
```

Payload bindings are plain locals: `Circle(r)` binds `r` to the payload. Arm bodies can be any expression or statement block.

## switch on enums

Simple enums also work with `switch` (`case Color.Green:`):

```core
switch c {
    case Color.Red:   say "r"
    case Color.Green: say "g"
    default:          say "?"
}
```

**`switch` is for simple enums only** — a data-carrying variant in a `case` is an error (`variant 'Circle' carries data; construct it: 'Shape.Circle(...)'`). Data-carrying enums need `match`.

## Enums as values

Enums are plain values — copyable, storable in structs and arrays, passable by value:

```core
enum Color { Red, Green, Blue }
struct Wrap { s: Color }

func main() {
    cs: [Color; 3] = [Color.Red, Color.Green, Color.Blue]
    w = Wrap { s: Color.Blue }
    say w.s as i32       // 2
    for x in cs { say x as i32 }
}
```

## When data-carrying enums beat classes

| Need | Reach for |
|---|---|
| A value is *one of* a fixed set of shapes | data-carrying enum + `match` |
| Heterogeneous list items | enum variants as the element type |
| "may be absent / may fail" | `Option<T>` / `Result<T, E>` (prelude) |
| True subtype polymorphism, open set of types | [classes + interfaces](oop.md) |

Enums are a **closed** set: every producer and consumer is known at compile time. Interfaces model *open* sets (any future class can implement them).

## Complete working example

```core
// enums.cr
enum Color { Red, Green, Blue }

enum Shape {
    Circle(f64),
    Rect(f64, f64),
    Point
}

struct Wrap { s: Shape }

func describe(c: Color) -> string {
    return match c {
        Red   { "red" }
        Green { "green" }
        Blue  { "blue" }
    }
}

func area(s: Shape) -> f64 {
    return match s {
        Circle(r)  { 3.14159 * r * r }
        Rect(w, h) { w * h }
        Point      { 0.0 }
    }
}

func tag(s: Shape) -> i32 {
    match s {
        Circle(r)  { return 0 }
        Rect(w, h) { return 1 }
        Point      { return 2 }
    }
}

func main() {
    c = Color.Green
    say describe(c)               // green
    say c as i32                  // 1
    say describe(Color.Blue)      // blue

    say area(Shape.Circle(1.0))   // 3.14...
    say area(Shape.Rect(3.0, 4.0))// 12
    say area(Shape.Point)         // 0
    say tag(Shape.Rect(1.0, 2.0)) // 1

    // prelude enums
    o: Option<i32> = Option<i32>.Some(3)
    match o {
        Some(v) { say v }         // 3
        None    { say "none" }
    }
    r: Result<i32, string> = Result<i32, string>.Ok(7)
    match r {
        Ok(v)  { say v }          // 7
        Err(e) { say e }
    }

    // enums as data
    w = Wrap { s: Shape.Point }
    say area(w.s)                 // 0
    cs: [Color; 3] = [Color.Red, Color.Green, Color.Blue]
    for x in cs { print(describe(x) + " ") }
    print("\n")                   // red green blue
}
```

## Common mistakes

- **`switch` with data-carrying variants.** Use `match` — switch cases can't bind payloads.
- **Non-exhaustive `match`.** Missing an enum variant is a compile error — add the case or `_`.
- **Comparing data-carrying enums with `==`.** Not supported (they're tagged unions) — compare via `match`.
- **Variant names without the type.** Inside `match` arms, bare names (`Circle(r)`) are the pattern; for *construction*, use `Shape.Circle(1.0)`. Generic variants need the full form: `Option<i32>.Some(5)`.
- **Forgetting `as i32` to print a simple enum** — `say` has no enum overload.

## Performance notes

- Simple enums are `i32` — pass in registers, compare with one instruction.
- Data-carrying enums are a tag + max-payload storage: `Shape` is 24 bytes (tag + two f64s, rounded by alignment). No heap, no pointers.
- `match` compiles to a jump table on the tag; payload loads are direct field offsets.
- Exhaustive matches with no `_` let the compiler assume no other tags exist — branch-free code is possible.

## When to use / not use

- **Use enums** for state machines, token types, message types, AST nodes, and every "one of" situation.
- **Use `Option<T>`/`Result<T, E>`** instead of sentinel values (`-1`, `null`) — see [error-handling.md](error-handling.md).
- **Don't** use an enum when the set of cases is open (plugins, user-defined types) — use [interfaces](oop.md).
- **Don't** stuff huge payloads in variants — store a pointer or reduce the payload.

Next: [Error handling](error-handling.md).
