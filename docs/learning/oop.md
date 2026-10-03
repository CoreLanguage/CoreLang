# OOP: virtual dispatch, interfaces, traits

[Classes](classes.md) give you state + inheritance. This page is about **polymorphism**: deciding which implementation runs *at runtime*, and what it costs.

## virtual and override

By default, method calls are **statically dispatched** — the compiler knows the exact type and emits a direct call. Mark a method `virtual` to make it dispatch dynamically when called through a base-class pointer:

```core
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

func main() {
    d = Dog { }
    say d.speak()             // static: Woof

    a: ptr<Animal> = &d       // upcast to the base (implicit)
    say a.speak()             // DYNAMIC: Woof — the override runs
    say a.describe()          // virtual call inside a plain method also dispatches
}
```

Rules:

- `virtual func` — base declares it dispatchable; provides a default body.
- `override func` — derived class replaces it. The signature must match.
- Non-virtual methods never dispatch: through a base pointer you'd get the base's version.
- `abstract func` — a `virtual` with **no body**; the class becomes **abstract** (can't be instantiated, only derived):

```core
abstract class Base {
    pub virtual func area() -> f64        // no body
    pub func tag() -> string { return "base" }
}

class Sq : Base {
    side: f64
    pub func init(s: f64) { self.side = s }
    pub override func area() -> f64 { return self.side * self.side }
}

func main() {
    sq = Sq { side: 3.0 }
    b: ptr<Base> = &sq
    say b.area()      // 9 — dynamic
    say b.tag()       // "base" — static
}
```

## Vtables: what dynamic dispatch costs

A class with any `virtual`/`abstract` method (or deriving from such a class) is **polymorphic**: every instance carries a **vtable pointer at offset 0** — one hidden word before your fields. The vtable is a static array of function pointers, one per virtual method.

```core
class Animal {                     // polymorphic
    name: string                   // offset 8: vtable ptr occupies offset 0
    pub virtual func speak() -> string { ... }
}
```

Consequences:

- Every polymorphic instance costs 8 extra bytes.
- A virtual call is two loads (vtable, slot) + an indirect call — cheap, but opaque to the optimizer (no inlining across it).
- Constructors store the class vtable on every exit; a derived init runs *after* the base's, so the most-derived vtable wins.
- Classes with **no** virtual methods have **no vtable and no overhead** — instances are plain field storage.

## interfaces and traits

```core
interface Shape {              // pure contract: no bodies
    func area() -> f64
}

trait Named {                  // trait: contract + default bodies
    func name() -> string { return "unnamed" }
}

class Cat : Animal, Shape, Named {
    pub func init() { Animal.init("cat") }
    pub override func speak() -> string { return "Meow" }
    pub func area() -> f64 { return 1.0 }
    pub func name() -> string { return "paws" }
}
```

- `class C : Base, Iface1, Iface2` — one base class, then any number of interfaces/traits.
- A class implements an interface by providing all its methods.
- **Trait defaults**: if the class doesn't provide `name()`, the trait's body runs.

### Interface values are fat pointers

`obj as Interface` creates a 16-byte **fat pointer**: `{object pointer, itable pointer}` — the itable holds the interface's method implementations for that concrete class:

```core
// standalone version (trimmed Cat: same shape as the class above, less ceremony)
interface Shape { func area() -> f64 }

class Cat : Shape {
    pub func init() { }
    pub func area() -> f64 { return 1.0 }
}

func total_area(shapes: [Shape; 2]) -> f64 {
    return shapes[0].area() + shapes[1].area()
}

func main() {
    c = Cat { }
    s: Shape = c as Shape     // fat pointer: {&c, Cat's Shape itable}
    say s.area()              // 1 — goes through the itable

    shapes: [Shape; 2] = [c as Shape, c as Shape]
    say total_area(shapes)    // 2
}
```

You can get the concrete object back with an unsafe downcast (see [unsafe.md](unsafe.md)):

```core
interface Shouter { func shout() -> string }

class Loud : Shouter {
    pub func init() { }
    pub func shout() -> string { return "LOUD" }
}

func main() {
    l = Loud { }
    sh: Shouter = l as Shouter
    say sh.shout()                 // LOUD
    back = unsafe { sh as Loud }   // unwrap the fat pointer
    say back.shout()               // LOUD
}
```

> **v0.1 limitations:** generic interfaces aren't supported yet; there are no `as?`/optional casts — a bad downcast is your bug, not a runtime error. And unwrapping a fat pointer whose class **uses inheritance** currently miscompiles — when you need the concrete object back, keep a reference to it instead of unwrapping.

## Static vs dynamic: choosing

| | static dispatch (default) | dynamic (`virtual`/interfaces) |
|---|---|---|
| Call cost | direct call, inlines | 2 loads + indirect call |
| Object size | fields only | +8 bytes vtable ptr |
| Optimizable | fully | limited |
| Flexibility | fixed at compile time | runtime-selectable |

**Prefer static dispatch.** Reach for `virtual`/interfaces when:

- a container must hold *mixed* concrete types (`[ptr<Animal>; N]`, `[Shape; N]`),
- a plugin/callback must be provided by another module without generics,
- you genuinely need runtime selection.

For a single known implementation, or when all types are known at compile time, generics + static calls are faster and simpler (see [generics.md](generics.md)).

## Complete working example

```core
// oop.cr - a small zoo: inheritance, virtuals, interface + trait
interface Shape {
    func area() -> f64
}

trait Named {
    func name() -> string { return "unnamed" }
}

class Animal {
    name: string
    pub func init(name: string) { self.name = name }
    pub virtual func speak() -> string { return "..." }
    pub func describe() -> string { return self.name + " says " + self.speak() }
}

class Dog : Animal, Named {
    pub func init() { Animal.init("dog") }
    pub override func speak() -> string { return "Woof" }
    pub func name() -> string { return "rex" }
}

class Cat : Animal, Shape, Named {
    pub func init() { Animal.init("cat") }
    pub override func speak() -> string { return "Meow" }
    pub func area() -> f64 { return 1.0 }
    pub func name() -> string { return "paws" }
}

func main() {
    d = Dog { }
    say d.speak()             // Woof
    say d.describe()          // dog says Woof
    say d.name()              // rex (trait default overridden)

    a: ptr<Animal> = &d       // upcast
    say a.speak()             // dynamic dispatch: Woof

    c = Cat { }
    s: Shape = c as Shape     // fat pointer
    say s.area()              // 1
    n: Named = c as Named
    say n.name()              // paws

    zoo: [ptr<Animal>; 2] = [&d, &c]
    for animal in zoo { say animal.speak() }   // Woof, Meow
}
```

## Common mistakes

- **Forgetting `virtual`** on the base method — the derived `override` won't dispatch through base pointers. (The compiler rejects `override` of a non-virtual method.)
- **Forgetting `override`** in the derived class — the method silently shadows... no: the compiler requires `override` for matching virtuals; a signature mismatch means you wrote an unrelated method.
- **Instantiating an abstract class.** Classes with `abstract` methods can't be constructed — derive first.
- **Expecting interface values to be cheap as structs.** They're fat pointers; copying one copies 16 bytes, and calls go through itables.
- **Unsafe-unwrap to the wrong class** — `s as Cat` on a `Shape` backed by a `Sq` is undefined behavior. Only unwrap what you put in (and avoid unwrapping entirely on inherited classes in v0.1 — see the limitation above).
- **Assuming vtables for every class.** Only polymorphic classes pay; a class without virtuals is struct-cost.

## Performance notes

- Virtual calls prevent inlining and vectorization across the call. In hot loops over homogeneous data, prefer static dispatch or generics.
- Interface values cost one extra indirection vs virtual calls (itable fetch + slot fetch).
- Devirtualization: LLVM can sometimes prove the concrete type and fold the call — don't count on it.
- Keep vtables small: every virtual method is a slot in every instance's vtable (shared per class, so it's one word per method in the table, not per object).

## When to use / not use

- **Use virtual + inheritance** for classic type hierarchies with mixed-type containers.
- **Use interfaces** for contracts across unrelated class hierarchies; **traits** when a sensible default implementation exists.
- **Avoid** OOP for its own sake: Core's structs + free functions + generics cover most designs with less machinery.
- For data-heavy code (sorting, simulation inner loops), static dispatch wins — see [algorithms.md](algorithms.md).

## Exercises

1. Define an `interface Speaker { func sound() -> string }`, implement it on two unrelated classes, store both as `Speaker` values in a `[Speaker; 2]` array, and call `sound()` through each element.
2. Write a trait `Described { func describe() -> string { return "(no description)" } }`, implement it on one class that overrides it and one that doesn't; call through trait values.
3. Make `Shape` an **abstract class** with `virtual area()` plus a concrete `tag()`; implement `Circle`/`Square`; write a function summing areas through base-class pointers.
4. Time 1M virtual calls vs 1M static struct-method calls with `time.monotonic_ms()` — quantify the dispatch overhead.
5. Store mixed types (`Circle`, `Square`) together as interface values in a fixed array and compute the total area.

Next: [Generics](generics.md).
