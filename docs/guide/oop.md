# Classes and OOP

Core has single inheritance with vtable dispatch, plus interfaces and
traits for contracts. Classes are the only polymorphic types.

## Classes

Class members are private by default; `pub` makes them visible outside
the module.

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
```

- Constructors are `func init(...)` methods. The class literal
  `Dog { }` zero-initializes fields and calls the `init` whose
  parameters all have defaults. Call the base constructor as
  `Animal.init("dog")`; `self` passes through implicitly.
- `virtual func` dispatches dynamically; `override func` replaces the
  base's vtable slot; `abstract func` has no body and makes the class
  non-instantiable.
- Every `init` stores its own class's vtable into `self`, and the
  most-derived constructor's store wins, so virtual calls during
  construction are safe.

## Vtables and dispatch

A class is polymorphic when it declares a `virtual` or `abstract`
method, is `abstract`, or derives from a polymorphic class. Polymorphic
classes carry a vtable pointer at offset 0; non-polymorphic classes do
not. Calls dispatch through the vtable exactly when the method is in
the static type's vtable; everything else is a direct call. That means
`d.speak()` on a `Dog` variable is a direct call, while
`a.speak()` through `ptr<Animal>` goes through the vtable.

```core
func main() {
    d = Dog { }
    a: ptr<Animal> = &d        // upcast, implicit in argument position too
    say a.speak()              // dynamic dispatch: "Woof"
    say d.describe()           // "dog says Woof"
}
```

Classes are values: copying copies the vptr and fields. Use
`ptr<Class>` for shared, polymorphic objects, as above.

Base implementations are reachable with `BaseName.method(...)`:

```core
class Puppy : Dog {
    pub override func speak() -> string { return Dog.speak() + "!" }
}
```

## Interfaces

An `interface` is a pure method contract: prototypes only, no bodies.
An interface value is a fat pointer, `{object ptr, itable ptr}`, 16
bytes, created by `obj as Interface` (implicit in argument position).
A method call loads the method's slot from the itable.

```core
interface Shape {
    func area() -> f64
}

class Cat : Animal, Shape {          // base class, then interfaces
    pub func init() { Animal.init("cat") }
    pub override func speak() -> string { return "Meow" }
    pub func area() -> f64 { return 1.0 }
}

func print_area(s: Shape) { say s.area() }

func main() {
    c = Cat { }
    print_area(c)                    // implicit conversion to Shape
}
```

`obj as Interface` requires the class to implement the interface;
otherwise it is a compile error. Unwrapping an interface value back to
a concrete class requires `unsafe` and performs no runtime check.

## Traits

A `trait` is an interface where methods may have default bodies. A
class that does not implement the method gets the default.

```core
trait Named {
    func name() -> string { return "unnamed" }
}

class Dog : Animal, Named {
    pub func init() { Animal.init("dog") }
    pub func name() -> string { return "rex" }   // overrides the default
}
```

## When to use what

- Struct for data with static methods; it has no overhead.
- Class when you need inheritance or runtime dispatch.
- Interface for contracts with no default behavior; trait when a
  sensible default exists.
- Polymorphic objects are manipulated through `ptr<Base>`; an array of
  mixed types is an array of pointers:

```core
zoo: [ptr<Animal>; 2] = [&d, &c]
for animal in zoo { say animal.speak() }
```

Layout details: [language/abi.md](../language/abi.md) §4-§5.
