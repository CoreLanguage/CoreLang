# Structs

Structs are Core's record type: named fields, C-compatible memory layout, and methods with static dispatch. No inheritance (that's [classes](classes.md)), no hidden pointers, no constructors unless you write one.

## Definition and literals

```core
struct Vec2 {
    x: f64
    y: f64
}

v = Vec2 { x: 3.0, y: 4.0 }
```

- **All fields are required** in a literal. No partial construction — if that's inconvenient, write an `init` (structs can have them too, like classes do) or build through a factory function.
- Structs are **values**: assignment copies, passing to a function copies, `sizeof` reports the full field storage.

## Layout: predictable, C-compatible

Fields are laid out in declaration order with **natural alignment** — the same layout a C compiler would choose. `sizeof(T)` and `alignof(T)` give you exact numbers, and a Core struct is ABI-compatible with the matching C struct:

```core
struct Vec2 { x: f64, y: f64 }   // sizeof = 16, alignof = 8 — same as C
```

### `@packed`

`@packed` removes inter-field padding: fields are squeezed byte after byte. Use it for wire formats, file headers, and device registers — where the byte offsets are *specified*, not chosen:

```core
@packed struct Header {
    magic: u16
    version: u8
    flags: u8
}
// sizeof(Header) == 4 — no padding
```

Packed structs can be **misaligned** in arrays (element size 5 sits at odd offsets); access may be slower or (on some targets) require care — see [low-level-programming.md](low-level-programming.md).

## Methods

Structs can declare methods. Inside a method, `self` refers to the value — field access and method calls on `self` auto-deref, so it behaves like a pointer to the struct:

```core
struct Vec2 {
    x: f64
    y: f64
    pub func dot(o: Vec2) -> f64 { return self.x * o.x + self.y * o.y }
    pub func length_sq() -> f64 { return self.x * self.x + self.y * self.y }
    pub func length() -> f64 { return math.sqrt(self.length_sq()) }
}

func main() {
    v = Vec2 { x: 3.0, y: 4.0 }
    say v.dot(Vec2 { x: 3.0, y: 4.0 })   // 25
    say v.length()                        // 5
}
```

Method calls use dot syntax and **static dispatch** — the compiler knows the concrete type at the call site, so calls are direct (no vtable; see [oop.md](oop.md) for dynamic dispatch).

Method parameters are ordinary values: `dot(o: Vec2)` copies its argument. To mutate through a method, take a pointer parameter (`func push(s: ptr<Stack>, v: i32)`) — see [references.md](references.md).

## Field access and mutation

```core
v = Vec2 { x: 3.0, y: 4.0 }
v.x = 0.0              // mutate a field directly

w = v                  // copies the whole struct
w.x = 9.0
say v.x                // 0 — w is a separate copy

p = &v                 // pointer to the struct
p.y = 1.0              // auto-deref through the pointer
say v.y                // 1
```

## Nesting

Structs nest by value — a field can be another struct, stored inline:

```core
struct Line {
    a: Vec2
    b: Vec2
}
l = Line { a: Vec2 { x: 0.0, y: 0.0 }, b: Vec2 { x: 3.0, y: 4.0 } }
say l.b.x             // 3
l.b.x = 6.0           // reach through nested fields
```

## Visibility

Struct members are **public by default**. Use `private` to hide fields from other modules when you want controlled access — mirror with getter methods. (Classes invert the default: private by default, `pub` to expose — see [classes.md](classes.md).)

## Complete working example

```core
// structs.cr - geometry with structs, methods, nesting
import math

struct Vec2 {
    x: f64
    y: f64
    pub func dot(o: Vec2) -> f64 { return self.x * o.x + self.y * o.y }
    pub func length_sq() -> f64 { return self.x * self.x + self.y * self.y }
    pub func length() -> f64 { return math.sqrt(self.length_sq()) }
}

@packed struct ColorRGB {
    r: u8
    g: u8
    b: u8
}

struct Line {
    a: Vec2
    b: Vec2
}

func length(l: Line) -> f64 {
    dx = l.b.x - l.a.x
    dy = l.b.y - l.a.y
    return math.sqrt(dx * dx + dy * dy)
}

func main() {
    l = Line { a: Vec2 { x: 0.0, y: 0.0 }, b: Vec2 { x: 3.0, y: 4.0 } }
    say length(l)         // 5
    l.b.x = 6.0
    say length(l)         // 7.211...

    v = Vec2 { x: 3.0, y: 4.0 }
    say v.dot(Vec2 { x: 3.0, y: 4.0 })   // 25
    say v.length()                        // 5

    say sizeof(Vec2)      // 16
    say sizeof(ColorRGB)  // 3 — packed, no padding
    c = ColorRGB { r: 255, g: 128, b: 0 }
    say c.r               // 255

    w = v                 // by-value copy
    w.x = 0.0
    say v.x               // 3 — w was a copy

    pv = &v               // pointer auto-deref on field access
    pv.y = 9.0
    say v.y               // 9
}
```

## Common mistakes

- **Partial literals.** `Vec2 { x: 1.0 }` is an error — all fields required.
- **Assuming reference semantics.** `w = v` copies; mutate through `&v` or a method taking a pointer.
- **Padding surprises in file formats.** A plain struct may have padding; `@packed` when the layout is specified externally.
- **Passing `self` as a value argument** inside methods — `self` behaves like a pointer; pass individual fields instead.
- **Forgetting `import math`** when a method body uses `math.sqrt` — module imports are per-file.

## Performance notes

- Structs have zero abstraction cost: fields compile to direct offsets, methods to direct calls that inline at `-O2`.
- Pass-by-value copies the whole struct; for anything beyond ~2 machine words, pass `ptr<T>` instead.
- `@packed` trades load/store efficiency for exact layout — fine for headers, slower in hot data paths.

## When to use / not use

- **Use structs** for data: vectors, records, configuration, C-ABI boundaries, buffers with a length field.
- **Use classes** when you need inheritance, virtual dispatch, or private-by-default encapsulation.
- **Don't** use `@packed` "just in case" — natural alignment is faster; pack only when the format demands it.

Next: [Pointers](pointers.md).
