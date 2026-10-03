# Concurrency

Core concurrency is **OS threads, mutexes, and atomics** — thin wrappers over pthreads via the `thread` module. No async runtimes, no schedulers, no GC coordination. Predictable, low-level, and entirely in your hands.

## The model

- `thread.spawn(f)` runs a function value on a new OS thread; returns an opaque handle.
- `thread.join(handle)` blocks until it finishes.
- Shared state = shared **pointers**. There is no message passing runtime — you coordinate with `Mutex`, `RwLock`, `Cond`, or atomics.
- All operations are sequentially consistent (`seq_cst`).

## The capture-by-value rule (read this first)

Lambdas capture enclosing locals **by value** — the lambda gets a *copy* made at creation time:

```core
func main() {
    mut local = 1
    snap = func() -> i32 { return local }
    local = 2
    say snap()    // 1 — the lambda sees the OLD value
    say local     // 2
}
```

This has a crucial consequence for threads: a lambda that captures a local struct **copies the struct** — the thread would mutate its own copy, not yours. The canonical fix: capture a **pointer** to the state you want to share:

```core
acc = Account { }
acc.init(0)
pa = &acc                       // the pointer is copied BY VALUE — fine
w1 = thread.spawn(func() {
    for i in 0..1000 { deposit(pa, 1) }   // pa points at the SAME Account
})
```

Capturing `pa` copies the *pointer* (8 bytes), and both threads' copies point at the same `Account`. This is **the** pointer-sharing pattern.

## Mutex: protecting shared state

```core
import thread

struct Account {
    balance: i64
    lock: Mutex
    pub func init(v: i64 = 0) {
        self.balance = v
        self.lock.init()
    }
}

func deposit(acc: ptr<Account>, amount: i64) {
    acc.lock.lock()
    acc.balance += amount
    acc.lock.unlock()
}

func main() {
    acc = Account { }
    acc.init(0)
    pa = &acc
    w1 = thread.spawn(func() { for i in 0..1000 { deposit(pa, 1) } })
    w2 = thread.spawn(func() { for i in 0..1000 { deposit(pa, 1) } })
    thread.join(w1)
    thread.join(w2)
    say acc.balance    // 2000 — always, no torn updates
}
```

`Mutex` API: `init()`, `lock()`, `unlock()`, `deinit()`. Also available: `RwLock` (`read/write/unlock/deinit`) for read-mostly data, and `Cond` (`wait(m)`, `signal()`, `broadcast()`, `deinit()`).

Lock discipline:

- **Never share a pointer without a lock or atomics.** Two threads writing `*ptr += 1` is a data race — undefined behavior, not "maybe slow".
- Lock, do the minimum, unlock. No re-entrancy (pthread's default mutex isn't recursive).
- `deinit()` the mutex when its owner tears down.

## Atomics: lock-free counters

`AtomicI32`, `AtomicI64`, `AtomicBool`, `AtomicUsize` — every op sequentially consistent:

```core
import thread

func main() {
    a = AtomicI32 { }
    a.init(0)
    pa = &a
    t1 = thread.spawn(func() { for i in 0..50000 { pa.add(1) } })
    t2 = thread.spawn(func() { for i in 0..50000 { pa.add(1) } })
    thread.join(t1)
    thread.join(t2)
    say a.load()    // 100000 — correct without locks
}
```

Per type (`T` = the wrapped primitive): `load() -> T`, `store(v: T)`, `add(v: T) -> T` / `sub` (return the **previous** value), `swap(v: T) -> T`, and `compare_exchange(expected: T, new: T) -> T` (CAS — stores `new` if current equals `expected`, returns the old value either way).

Raw builtins (work on any `ptr<T>` to an aligned `T`): `atomic_load(p)`, `atomic_store(p, v)`, `atomic_add`, `atomic_sub`, `atomic_swap`, `atomic_cas`, plus `atomic_fence()`.

A compare-and-swap retry loop:

```core
// increment only if the value is still positive
mut cur = a.load()
while cur > 0 {
    next = a.compare_exchange(cur, cur + 1)
    if next == cur { break }      // our write landed
    cur = next                    // someone else moved it; retry
}
```

## Spawning patterns

- **Worker over shared state:** capture one pointer, protect with mutex or atomics (above).
- **Fan-out/fan-in:** spawn N workers, join all, reduce results (each worker writes to its own slot — no lock needed for distinct indices).
- **Never** return pointers to thread-locals from a thread's lambda: the enclosing scope may die first; heap-allocate shared state.

## Data races: what actually happens

A race is two unsynchronized accesses, one writing. In Core this is **undefined behavior** — the compiler may reorder, cache in registers, or tear 64-bit ops on 32-bit targets (not on current 64-bit targets for aligned words, but don't rely on it). Symptoms: "impossible" values, counts that come out different on each run, crashes far from the bug. Prevention is cheap: pointers + `Mutex`, or atomics for single counters/flags.

## Complete working example

```core
// threads.cr - mutex-protected counter + atomic flag
import thread
import time

struct Counter {
    n: i64
    lock: Mutex
    pub func init() { self.n = 0; self.lock.init() }
}

func bump(c: ptr<Counter>) {
    c.lock.lock()
    c.n += 1
    c.lock.unlock()
}

func main() {
    c = Counter { }
    c.init()
    pc = &c

    workers: [ptr<void>; 4] = [null, null, null, null]
    for i in 0..4 {
        workers[i] = thread.spawn(func() {
            for j in 0..2500 { bump(pc) }
        })
    }
    for i in 0..4 { thread.join(workers[i]) }
    say c.n          // 10000

    flag = AtomicBool { }
    done = thread.spawn(func() {
        time.sleep_ms(10 as u64)
        flag.store(true)
    })
    while !flag.load() { time.sleep_ms(1 as u64) }
    thread.join(done)
    say "flag set"
}
```

Verified: prints `10000` then `flag set`.

## Common mistakes

- **Expecting captures to see later mutations.** Capture is by value; share through pointers.
- **Sharing a pointer without synchronization.** `for i in 0..N { *shared += 1 }` from two threads loses updates. Mutex or atomic — pick one.
- **Forgetting `thread.join`.** Unjoined threads may be killed at exit mid-work.
- **Locking twice on one thread** — deadlocks forever (no recursive mutexes).
- **`Cond.wait` without the mutex** — the API takes the mutex (`wait(m)`) and needs it held.
- **Assuming `i64` reads are atomic.** Aligned word loads usually are, but the *sequencing* isn't — use `AtomicI64`/`atomic_load` for cross-thread communication.
- **Returning after `deinit()` on a still-locked mutex.** Order: unlock → join → deinit.

## Performance notes

- Thread spawn is a syscall (pthread_create ~10–50µs) — spawn long-lived workers, not per-item.
- Uncontended lock/unlock is ~20ns; contended locks serialize — shrink critical sections.
- Atomics compile to `lock`-prefixed instructions: ~10–20ns contended, cheaper than a mutex for single-word state.
- False sharing: two atomics on the same 64-byte cache line thrash; pad hot counters apart (see [low-level-programming.md](low-level-programming.md)).
- `seq_cst` everywhere is the safe default; there are no acquire/release variants in v0.1 — don't hand-roll weaker orderings with fences unless you really know.

## When to use / not use

- **Use threads** for CPU-parallel work (chunk an array, spawn K workers) and blocking IO.
- **Use atomics** for counters, flags, and lock-free single-word state.
- **Use mutexes** for compound invariants (more than one field must change together).
- **Don't** build actor frameworks or async runtimes — Core gives you the primitives; keep architectures simple.
- Single-threaded deterministic code is easier to debug — reach for threads only with a parallelism-shaped problem.

Next: [FFI](ffi.md).
