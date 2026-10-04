// Function calls with an opaque (volatile) loop-carried value so neither
// compiler can constant-fold the loop away.
// (a) small direct calls (inlining), (b) virtual dispatch (vtable),
// (c) function values (closure ABI indirect call).
func add_small(a: i64, b: i64) -> i64 { return a + b }

abstract class Base {
    pub virtual func step(x: i64) -> i64 { return x + 1 }
}

class Impl : Base {
    pub override func step(x: i64) -> i64 { return x + 1 }
}

func main() {
    ctrl: i64 = 0
    unsafe { volatile_store(&ctrl, 1) } // runtime-opaque: value 1

    mut x: i64 = 0
    mut i: i64 = 0
    while i < 100000000 {
        x = add_small(x, ctrl)
        i += 1
    }
    say x

    obj = Impl { }
    b: ptr<Base> = &obj
    x = 0
    i = 0
    while i < 100000000 {
        x = b.step(x)
        i += 1
    }
    say x

    f = func(a: i64, b: i64) -> i64 { return a + b }
    x = 0
    i = 0
    while i < 100000000 {
        x = f(x, ctrl)
        i += 1
    }
    say x
}
