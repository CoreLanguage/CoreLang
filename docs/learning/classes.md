# Classes

Classes add **encapsulation and inheritance** on top of structs: fields, methods, constructors (`init`), static methods, `pub`/private visibility, and single inheritance with `super` calls. For the polymorphism machinery (virtual/override, interfaces, vtables) see [oop.md](oop.md).

## Definition

```core
class Animal {
    name: string                                     // field
    pub func init(name: string) { self.name = name } // constructor
    pub virtual func speak() -> string { return "..." }
    pub func describe() -> string { return self.name + " says " + self.speak() }
}
```

- Fields are declared like struct fields.
- Methods are functions inside the class; `self` refers to the instance (field access and method calls on `self` auto-deref).
- **Class members are private by default; `pub` makes them public.** (Structs are the opposite: public by default.) v0.1 note: cross-module access to non-`pub` members isn't rejected yet — treat missing `pub` as internal by convention; the hard visibility boundary is the module level (`pub func` / `pub class` / `pub struct` — see [modules.md](modules.md)).
- Classes themselves are module-private unless declared `pub class`.

## Construction: `init` constructors

A class literal `Dog { ... }` invokes an `init` method. `init` methods:

- are named exactly `init`
- assign `self.field` directly (no `return`)
- may take parameters (optionally defaulted) — the literal's `field: value` pairs are passed as arguments
- may call the base class's init via `Animal.init(...)` — see inheritance below

```core
class Temp {
    celsius: f64
    pub func init(c: f64 = 0.0) { self.celsius = c }
    pub func c() -> f64 { return self.celsius }
}

func main() {
    t = Temp { }            // init() with all defaults -> 0.0
    say t.c()
    t2 = Temp { c: 36.6 }   // init(c: 36.6)
    say t2.c()              // 36.6
}
```

`Temp { }` requires an init callable with **no arguments** (all parameters defaulted). If every init needs arguments, the literal must supply them (`Temp { c: 36.6 }`) — otherwise the compiler tells you to `alloc<T>()` + call `init` explicitly.

## Static methods

`static func` declares a method with no `self` — a namespaced function on the class:

```core
class Temp {
    celsius: f64
    pub func init(c: f64 = 0.0) { self.celsius = c }
    static func from_f(f: f64) -> Temp {      // factory
        t = Temp { c: f }
        return t
    }
}

func main() {
    boiling = Temp.from_f(100.0)
    say boiling.c()           // 100
}
```

Static methods are called through the class name (they're also reachable through an instance).

## Single inheritance

```core
class Dog : Animal {
    pub func init() { Animal.init("dog") }           // super constructor call
    pub override func speak() -> string { return "Woof" }
}
```

- `class Dog : Animal` — one base class, declared after the colon.
- The derived `init` calls the base `init` by name: `Animal.init("dog")` runs the base constructor on `self`.
- The base's fields and non-private methods are inherited; `override` replaces a `virtual` base method (see [oop.md](oop.md)).

## Methods and mutation

Methods mutate fields through `self` directly:

```core
class Counter {
    n: i32
    pub func init() { self.n = 0 }
    pub func inc() { self.n += 1 }
    pub func get() -> i32 { return self.n }
}

func main() {
    c = Counter { }
    c.inc()
    c.inc()
    say c.get()    // 2
}
```

Class instances are values too — `d = Dog { }` is a stack value; pass `&d` when a function needs to mutate or avoid a copy (see [references.md](references.md)).

## Complete working example

```core
// classes.cr
class Animal {
    name: string
    pub func init(name: string) { self.name = name }
    pub virtual func speak() -> string { return "..." }
    pub func describe() -> string { return self.name + " says " + self.speak() }
}

class Dog : Animal {
    pub func init() { Animal.init("dog") }
    pub override func speak() -> string { return "Woof" }
}

class Cat : Animal {
    pub func init() { Animal.init("cat") }
    pub override func speak() -> string { return "Meow" }
}

class Counter {
    n: i32
    pub func init() { self.n = 0 }
    pub func inc() { self.n += 1 }
    pub func get() -> i32 { return self.n }
}

class Temp {
    celsius: f64
    pub func init(c: f64 = 0.0) { self.celsius = c }
    pub func c() -> f64 { return self.celsius }
    static func from_f(f: f64) -> Temp {
        t = Temp { c: f }
        return t
    }
}

func main() {
    d = Dog { }
    say d.speak()       // Woof
    say d.describe()    // dog says Woof

    c = Cat { }
    say c.describe()    // cat says Meow

    counter = Counter { }
    counter.inc()
    counter.inc()
    counter.inc()
    say counter.get()   // 3

    t = Temp { c: 36.6 }
    say t.c()           // 36.6
    say Temp.from_f(100.0).c()   // 100

    // polymorphism preview: a base-class pointer sees overrides
    zoo: [ptr<Animal>; 2] = [&d, &c]
    for animal in zoo { say animal.speak() }   // Woof, Meow
}
```

## Common mistakes

- **Forgetting `self.`** — fields are always accessed through `self` inside methods.
- **`init` with `return`.** init assigns fields; it doesn't return the object.
- **`Dog { }` with no zero-arg init.** Either default all init params, or supply them in the literal (`Dog { name: "rex" }` style requires the init to take them).
- **Forgetting `pub`** on methods you call from other modules (and on the class itself).
- **Expecting multiple inheritance.** One base class only; after a comma come interfaces/traits (see [oop.md](oop.md)).

## Performance notes

- Non-virtual methods compile to plain function calls — same as structs, zero overhead, inline at `-O2`.
- A class instance is its fields, laid out like a C struct; no hidden headers unless the class is polymorphic (then a vtable pointer sits at offset 0 — see [oop.md](oop.md)).
- Construction is a normal function call; nothing hidden allocates.

## When to use / not use

- **Use a class** when state and the operations on it belong together, when you need inheritance, or when private-by-convention encapsulation helps.
- **Use a struct** for plain data — less ceremony, public fields, C-compatible by default.
- Don't build deep hierarchies in Core; prefer composition (a field) + static dispatch unless you specifically need polymorphism.

Next: [OOP: virtual dispatch, interfaces, traits](oop.md).
