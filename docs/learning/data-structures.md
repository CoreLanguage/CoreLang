# Data structures

Core ships no collection library — by design. You build exactly the structure you need, with exactly the memory policy you want. This page gives you the four canonical patterns: growable list, stack, queue, and hash map, plus the arena sketch from [memory-management.md](memory-management.md).

## The growable list (dynamic array)

Heap buffer + length + capacity, doubling on demand:

```core
import memory

struct List {
    data: ptr<i32>
    len: usize
    cap: usize
}

func list_init(l: ptr<List>, cap: usize) {
    l.data = alloc_array<i32>(cap)
    l.len = 0
    l.cap = cap
}

func list_push(l: ptr<List>, v: i32) {
    if l.len == l.cap {
        l.cap = l.cap * 2
        l.data = realloc_array<i32>(l.data, l.cap)
    }
    l.data[l.len] = v
    l.len += 1
}

func list_get(l: ptr<List>, i: usize) -> i32 { return l.data[i] }
func list_free(l: ptr<List>) { free(l.data) }

func main() {
    mut l: List
    l.data = null
    l.len = 0
    l.cap = 0
    list_init(&l, 4)
    for i in 0..10 { list_push(&l, i * i) }
    say l.len            // 10
    say list_get(&l, 7)  // 49
    list_free(&l)
}
```

Shape to memorize: `ptr<T>` + `len` + `cap`, `push` grows ×2 via `realloc_array`, one `free`. Generalize with generics ([generics.md](generics.md)): `struct List<T> { data: ptr<T>, ... }` + `list_push<T>(...)`.

## Stack: fixed array + depth counter

When the max size is known, skip the heap entirely:

```core
struct Stack {
    items: [i32; 16]
    depth: i32

    pub func push(v: i32) -> bool {
        if self.depth >= 16 { return false }   // full
        self.items[self.depth] = v
        self.depth += 1
        return true
    }

    pub func pop() -> i32 {
        if self.depth == 0 { return 0 }        // empty (or use Option<i32>)
        self.depth -= 1
        return self.items[self.depth]
    }
}

func main() {
    s = Stack { items: [0; 16], depth: 0 }
    s.push(1)
    s.push(2)
    say s.pop()   // 2
    say s.pop()   // 1
}
```

## Queue: ring buffer

A circular buffer with head + count — O(1) enqueue/dequeue, no shifting:

```core
struct Queue {
    buf: [i32; 16]
    head: i32
    count: i32

    pub func push(v: i32) -> bool {
        if self.count == 16 { return false }   // full
        self.buf[(self.head + self.count) % 16] = v
        self.count += 1
        return true
    }

    pub func pop() -> i32 {
        if self.count == 0 { return 0 }
        v = self.buf[self.head]
        self.head = (self.head + 1) % 16
        self.count -= 1
        return v
    }
}

func main() {
    q = Queue { buf: [0; 16], head: 0, count: 0 }
    q.push(10)
    q.push(20)
    q.push(30)
    say q.pop()   // 10 (FIFO)
    say q.pop()   // 20
}
```

## Hash map: open addressing, no heap per entry

String→i32 map with linear probing. Slots live in one zeroed allocation; an empty slot is `used == false`:

```core
import memory

struct Slot {
    key: string
    value: i32
    used: bool
}

struct HashMap {
    slots: ptr<Slot>
    cap: usize     // power of two!
    count: usize
}

func hash(s: string, cap: usize) -> usize {
    mut h: usize = 5381
    for i in 0..len(s) { h = (h * 33 + s[i] as usize) & (cap - 1) }
    return h
}

func map_init(m: ptr<HashMap>, cap: usize) {
    m.slots = alloc_zeroed_array<Slot>(cap)
    m.cap = cap
    m.count = 0
}

func map_put(m: ptr<HashMap>, key: string, value: i32) {
    mut i = hash(key, m.cap)
    while m.slots[i].used {
        if str_eq(m.slots[i].key, key) { m.slots[i].value = value; return }
        i = (i + 1) & (m.cap - 1)      // wrap (cap is a power of two)
    }
    m.slots[i].key = key
    m.slots[i].value = value
    m.slots[i].used = true
    m.count += 1
}

func map_get(m: ptr<HashMap>, key: string) -> Option<i32> {
    mut i = hash(key, m.cap)
    while m.slots[i].used {
        if str_eq(m.slots[i].key, key) { return Option<i32>.Some(m.slots[i].value) }
        i = (i + 1) & (m.cap - 1)
    }
    return Option<i32>.None
}

func map_free(m: ptr<HashMap>) { free(m.slots) }

func main() {
    mut m: HashMap
    m.slots = null
    m.cap = 0
    m.count = 0
    map_init(&m, 16)
    map_put(&m, "alpha", 1)
    map_put(&m, "beta", 2)
    map_put(&m, "alpha", 3)     // update in place
    match map_get(&m, "alpha") {
        Some(v) { say v }        // 3
        None    { say "missing" }
    }
    match map_get(&m, "beta") {
        Some(v) { say v }        // 2
        None    { say "missing" }
    }
    match map_get(&m, "gamma") {
        Some(v) { say v }
        None    { say "missing" }
    }
    map_free(&m)
}
```

Notes: `used == false` doubles as the empty-slot marker (thanks to `alloc_zeroed_array`); lookups walk until an unused slot; **grow** by allocating a bigger table and re-putting everything when `count * 4 >= cap * 3` (75% load). This is a *learning-grade* map — no deletion (which needs tombstones), keys are views (see the string-lifetime note below).

## The arena: many objects, one free

From [memory-management.md](memory-management.md) — bump allocation for same-lifetime groups:

```core
struct Bump {
    base: ptr<u8>
    offset: usize
    cap: usize
    pub func init(capacity: usize = 1024) {
        self.base = alloc_aligned(capacity, 16)
        self.offset = 0
        self.cap = capacity
    }
    pub func alloc_raw(n: usize, align: usize) -> ptr<void> {
        mut aligned = (self.offset + align - 1) / align * align
        if aligned + n > self.cap { panic("bump out of memory") }
        self.offset = aligned + n
        unsafe { return (self.base + aligned) as ptr<void> }
    }
    pub func reset() { self.offset = 0 }
}
```

Build a `List` whose backing store comes from `arena.alloc_raw(...)` and an entire parse tree frees with one `reset()`.

## String keys and ownership

A hash map with `string` keys stores **views**. If the key came from `"literal"`, it lives forever — safe. If it came from a `+` concatenation, the view outlives nothing — the buffer isn't freed but also can't be reused. For owned keys, copy bytes into an arena block and store a view of that.

## Complete working example

The four programs above are complete and verified. A single-file kitchen-sink version (list + stack + queue + map, outputs `10, 49, 2, 1, 10, 20, 3, 2, missing`) is exactly the concatenation of the snippets with one `main` calling each section — as shown per-snippet.

## Common mistakes

- **Forgetting to init the struct fields to `null`/0** before `list_init` when the struct came from a bare local — Core doesn't zero locals for you (use `alloc_zeroed` on the heap or explicit assignment).
- **Forgetting `import memory`.** The alloc family lives in the `memory` module; `Option` is prelude.
- **Non-power-of-two hash capacity.** The `& (cap - 1)` trick requires it; otherwise use `% cap`.
- **Storing views of temporaries.** Keys/values that view concatenated strings are fine (leak-ish but valid); views of *freed* buffers are traps.
- **Double-free through two containers.** If two Lists share a backing pointer, exactly one frees.
- **Stack overflow via unbounded growth.** Fixed-array stacks/queues must check `full` before pushing.

## Performance notes

- Growable array with ×2 growth: amortized O(1) push; `realloc` often extends in place.
- Fixed-array stack/queue: zero allocation, cache-friendly, fastest possible.
- Open-addressing maps beat pointer-chasing (bucket-of-nodes) designs on modern CPUs — keep entries inline in one array.
- Iterate `List` via pointer (see [arrays.md](arrays.md)) for bounds-check-free hot loops — inside `unsafe`.

## When to use / not use

- **Build what you need** — a 40-line list beats a 4000-line library when 40 lines suffice.
- For one-off tasks, plain **fixed arrays** (see [arrays.md](arrays.md)) are usually enough.
- If your data structure needs **polymorphic elements**, store data-carrying [enums](enums.md) or interface fat pointers ([oop.md](oop.md)).
- For shared/mutable structures across threads: wrap access in a `Mutex` (see [concurrency.md](concurrency.md)) — no structure is thread-safe by itself.

Next: [Algorithms](algorithms.md).
