# Generics

Generic functions, structs, classes, and enums are monomorphized: the
compiler clones each used instantiation, re-checks it with the type
parameters substituted, and gives it a mangled symbol. There is no
runtime dispatch and no boxing. The cost is binary size; the payoff is
zero-overhead code and errors that point at the instantiation.

## Generic functions

```core
func max<T>(a: T, b: T) -> T {
    if a > b { return a }
    return b
}

func swap<T>(a: ptr<T>, b: ptr<T>) {
    t: T = *a
    *a = *b
    *b = t
}
```

Type arguments are inferred by unifying parameter types against the
arguments, or supplied explicitly:

```core
say max(3, 9)         // inferred T = i32
say max('a', 'z')     // inferred T = char
say max<i64>(3, 9)    // explicit
```

Failing to infer a parameter is a compile error ("cannot infer type
parameter 'T'"); supply it explicitly.

## Generic types

```core
struct Pair<A, B> {
    first: A
    second: B
}

class Box<T> {
    value: T
    pub func init(v: T) { self.value = v }
}

enum Option<T> { Some(T), None }
```

Type arguments on literals are usually inferred, but writing them makes
the intent unambiguous and is required when inference has nothing to
grab:

```core
p: Pair<string, i32> = Pair<string, i32> { first: "age", second: 30 }
say p.first
```

Generic methods work the same way; the prelude's `alloc<T>()` is one.

## No trait bounds

There are no trait bounds in v1. A generic body may use any operation
on a type parameter that the concrete argument supports, and errors
surface at instantiation time:

```core
func sum<T>(arr: ptr<T>, n: i32) -> i64 {
    mut total: i64 = 0
    for i in 0..n { total += arr[i] as i64 }
    return total
}
```

This compiles for `T = i32` and fails at instantiation for a type with
no `as`-compatible arithmetic. The tradeoff: no upfront contract
checking, in exchange for a much simpler type system.

## A realistic example

A generic dynamic array over the heap allocator:

```core
import memory

struct DynArray<T> {
    data: ptr<T>
    len: usize
    cap: usize

    pub func push(x: T) {
        if self.cap <= self.len {
            mut nc: usize = 4
            if self.cap > 0 { nc = self.cap * 2 }
            nd: ptr<T> = memory.alloc_array<T>(nc)
            for i in 0..self.len { nd[i] = self.data[i] }
            if self.cap > 0 { memory.free(self.data) }
            self.data = nd
            self.cap = nc
        }
        self.data[self.len] = x
        self.len += 1
    }

    pub func get(i: i32) -> T {
        return self.data[i]
    }
}
```

Each `DynArray<i64>`, `DynArray<string>`, and so on in the program
becomes a separate concrete struct in the binary.

## Rules and limits

- Monomorphization depth is capped at 64; exceeding it (usually a
  recursive generic expansion) is a compile error.
- Each instantiation is re-checked with the type parameters
  substituted; symbols are mangled per instantiation, so a `pub func
  parse<T>` exports one symbol per T actually used.
- Generic interfaces are not supported in v1.
- Because everything is cloned per instantiation, large generic bodies
  used with many types grow the binary. That is the cost of the model;
  keep generic code small and push type-specific work to non-generic
  helpers.

The end-to-end implementation notes live in
[internals/monomorphization.md](../internals/monomorphization.md).
