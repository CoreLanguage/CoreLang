# Structs and enums

## Structs

A struct is a value type with C-compatible layout: fields in
declaration order with natural alignment, no vtable, no header. Copying
a struct copies the bytes. `@packed` removes all padding (alignment 1).

```core
struct Point {
    x: i32
    y: i32
}

p = Point { x: 1, y: 2 }
p.x = 10
q = p              // a copy; p and q are independent
q.x = 99
say p.x            // 10
```

A struct literal must list every field that has no declared default.
Fields with defaults may be omitted:

```core
struct Config {
    name: string
    retries: i32 = 3
}

a = Config { name: "db", retries: 5 }
b = Config { name: "db" }        // retries defaults to 3
```

Omitting a field that has no default is a compile error.

## Methods

Struct members are public by default (`pub` is accepted for clarity).
Methods dispatch statically; there is no vtable on a plain struct.

```core
struct Point {
    x: i32
    y: i32

    pub func sum() -> i32 { return self.x + self.y }
    static func origin() -> Point { return Point { x: 0, y: 0 } }
}

func main() {
    p = Point.origin()
    say p.sum()          // 0
}
```

Instance methods receive `self: ptr<Point>`; calling a method on a
value passes its address automatically. `self.x` accesses fields,
`self.sum()` calls other methods.

If the type has an `init` method, a literal may omit fields; `init`
runs after the listed fields are stored, and unlisted fields get their
declared default or zero.

## Passing convention

Structs are values: assignment and argument passing copy the bytes.
For anything larger than a couple of words, pass a pointer and mutate
through it, as in the `Buffer` example in [memory.md](memory.md).

```core
func scale(pt: ptr<Point>, k: i32) {
    pt.x *= k
    pt.y *= k
}
```

## Enums

Simple enums are `i32`-backed. Variants are namespaced by their enum
(`Color.Red`); a bare `Red` works when the name is unique across all
visible enums, otherwise it is an ambiguity error and you qualify it.

```core
enum Color { Red, Green, Blue }

c = Color.Green
if c == Color.Green { say "go" }
```

`==` and `!=` work on payload-free enums only.

Data-carrying enums are tagged unions: an `i32` tag plus a payload
area. Constructing a variant zeroes the inactive payload bytes.

```core
enum Op {
    Add(i32, i32),
    Neg(i32),
    Id
}

func eval(e: Op) -> i32 {
    return match e {
        Add(a, b) { return a + b }
        Neg(v)    { return -v }
        Id        { return 7 }
    }
}
```

Construct with `Op.Add(2, 3)`. Generic enums infer their type arguments
from the payload: `Some(5)` builds an `Option<i32>` when that is the
only possibility, otherwise write `Option<i32>.Some(5)`.

Values of a data-carrying enum cannot be compared with `==`; use
`match`, which must be exhaustive:

```core
func describe(o: Option<i32>) -> string {
    return match o {
        Some(v) { "got " + to_string(v) }
        None    { "nothing" }
    }
}
```

`Option<T>` (`Some(T)`, `None`) and `Result<T, E>` (`Ok(T)`, `Err(E)`)
come from the prelude, no import needed.

## Methods on enums

Enums can declare methods the same way structs can; use `match` on
`self` to reach payloads:

```core
enum Shape {
    Circle(f64),
    Rect(f64, f64)

    func area() -> f64 {
        return match self {
            Circle(r)  { 3.0 * r * r }
            Rect(w, h) { w * h }
        }
    }
}
```

## Layout notes

- Struct fields are laid out in declaration order with natural
  alignment (C-compatible). `@packed` on the struct removes all
  padding, alignment 1.
- A data-carrying enum is an `i32` tag at offset 0 followed by the
  payload area at the next offset aligned to the most-aligned payload.
  `sizeof` covers tag plus the largest payload.
- Exact layouts, including mangling and vtable placement, are in
  [language/abi.md](../language/abi.md).
