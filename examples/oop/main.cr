// Classes, inheritance, virtual dispatch, interfaces, and traits.
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

class Cat : Animal, Shape {
    pub func init() { Animal.init("cat") }
    pub override func speak() -> string { return "Meow" }
    pub func area() -> f64 { return 1.0 }
}

func main() {
    d = Dog { }
    say d.speak()             // virtual: Woof
    say d.describe()          // dog says Woof
    say d.name()              // trait default overridden: rex

    a: ptr<Animal> = &d       // upcast
    say a.speak()             // dynamic dispatch: Woof

    c = Cat { }
    s: Shape = c as Shape     // interface value (fat pointer)
    say s.area()              // 1

    zoo: [ptr<Animal>; 2] = [&d, &c]
    for animal in zoo { say animal.speak() }
}
