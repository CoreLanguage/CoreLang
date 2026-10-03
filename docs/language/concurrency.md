# Core Concurrency Guide

Core's concurrency story is thin, explicit, and predictable: OS threads
(pthread), classic locks, and sequentially-consistent atomics. There is
no scheduler, no async runtime, no green threads, no GC coordination,
and nothing hidden.

Related: [SPEC.md](SPEC.md), [memory-model.md](memory-model.md)
(the memory model chapter applies per-thread).

---

## 1. The thread API

```core
import thread

func main() {
    w = thread.spawn(func() { ... })   // -> ptr<void>, an opaque handle
    thread.join(w)                     // blocks; frees the handle
}
```

- `spawn` takes a **closure value** of type `func()` — no parameters, no
  return value. It creates a new OS thread (`pthread_create`) that runs
  the closure once.
- The return value is a handle (malloc'd `pthread_t*`). `join` blocks
  until the thread finishes and frees the handle. **Join every thread
  you spawn** — there is no detach API in v1, and losing a handle leaks
  it (leaking is defined but permanent).
- Spawned threads start immediately. If a spawn fails (out of memory,
  pthread error), the runtime panics and aborts.
- Thread results: write to shared memory the main thread reads after
  `join` (see the pattern in §5), or use atomics.

The entry file's `main` runs on the process's main thread; `process.exit`
terminates the process from anywhere.

---

## 2. Capture semantics for closures (critical)

Lambdas capture enclosing locals **by value** at the moment the closure
is created. The captured values live in the closure's environment block;
they are copies that never alias the originals, and the originals' later
mutations are invisible to the closure (and vice versa).

Consequences:

- You cannot share a mutable counter by capturing it directly:

```core
mut count = 0                          // main's variable
w = thread.spawn(func() { count += 1 })
thread.join(w)
say count                              // prints 0
```

  This compiles, but the closure mutates **its own captured copy** — the
  increment is invisible to `main` and to other threads (verified: the
  closure's env block holds a private copy).
- The idiom is to share a **pointer to heap memory**:

```core
import memory
import thread

state = alloc<i64>()          // heap: one shared cell (global; see note)

func main() {
    *state = 0
    w = thread.spawn(func() {
        unsafe { *state += 1 }
    })
    thread.join(w)
    say (*state) as i64       // 1
}
```

  The **pointer** is captured by value; the pointee is shared.
- `self` in a method is captured by value like any local (the pointer is
  copied — the object itself is shared).
- Captured pointers must point at memory that outlives the thread:
  heap allocations, globals, or `tls` — not the spawning frame's locals.

---

## 3. Memory visibility

Core follows C11/pthread semantics:

- **Data races are undefined behavior**: unsynchronized concurrent
  access to the same non-atomic memory where at least one access is a
  write.
- Synchronization comes from exactly three tools:
  1. **Mutex/RwLock** — pthread locks; unlocking synchronizes-with the
     next lock, so everything a thread did before unlocking is visible
     after the corresponding lock.
  2. **Atomics** — seq_cst (§4); the only lock-free coordination.
  3. **Thread spawn/join** — everything the thread did before returning
     is visible after `join` returns.
- Plain (non-atomic, unlocked) reads of data another thread is writing
  are a race — even for "small" values, even if you "know the timing".
- `volatile` is **not** a synchronization tool
  (memory-model.md §10) — never use it for threads.
- Values a thread reads through plain loads may be cached in registers
  across loop iterations; the only guaranteed freshness comes from the
  three tools above.

---

## 4. Atomics (seq_cst only)

Builtins (compiler-implemented) and their wrapper classes:

```core
import thread

func main() {
    n = AtomicI32 { }              // zero-initialized; .init() optional
    n.add(5)                       // returns the previous value
    say n.load()                   // 5
    old = n.swap(9)
    cur = n.compare_exchange(9, 12)  // store 12 if current==9; returns old value
    n.store(0)
}
```

Atomics as globals work too — declare them with a type and initialize in
`main` (top-level code is not executed):

```core
import thread

counter: AtomicI32              // global, zero-initialized

func main() {
    counter.init(0)
    // ...
}
```

- Every operation is **sequentially consistent** — there is a single
  total order over all atomic operations, agreed by all threads. Core
  exposes no acquire/release/consume orderings in v1; if you need
  weaker orderings, drop to `asm` inside `unsafe`.
- `atomic_add/sub/swap` return the **previous** value;
  `atomic_cas(p, expected, new)` returns the old value (strong CAS — it
  never fails spuriously; compare the result to `expected` to detect
  success).
- Supported pointees: integers and `bool` (`AtomicBool` is stored as an
  `i8` cell). `f32`/`f64` atomics do not exist in v1 — use `atomic_cas`
  on a bit-cast integer or protect with a mutex.
- `atomic_fence()` is a full seq_cst fence.
- Atomics work on heap, stack, global, and `tls` memory alike (though
  `tls` + atomics across threads is pointless — each thread sees its
  own instance).

---

## 5. Mutexes, RwLocks, condition variables

```core
import thread

func main() {
    m = Mutex { }       // init/lock/unlock/deinit
    m.init()
    m.lock()
    // ... critical section ...
    m.unlock()
    m.deinit()          // destroys the pthread mutex and frees the handle
}
```

- `Mutex.lock()` blocks; default pthread semantics apply (no error
  checking, deadlock on double-lock by the same thread is **undefined**
  — with the default pthread type it deadlocks, it does not throw).
- `RwLock`: `read()` (shared), `write()` (exclusive), `unlock()`,
  `deinit()`.
- `Cond`: `wait(m: Mutex)` (atomically unlocks `m` and sleeps; wakes
  with `m` held), `signal()`, `broadcast()`, `deinit()`. Spurious
  wakeups are possible — always wait in a predicate loop.

Canonical producer/consumer shape:

```core
import thread

mut done: bool = false
mut items_ready: i32 = 0
m: Mutex              // zeroed globals; global initializers must be
c: Cond               // compile-time constants, so init() runs in main

func main() {
    m.init()
    c.init()
    worker = thread.spawn(func() {
        m.lock()
        while items_ready == 0 {          // predicate loop: spurious wakeups
            c.wait(m)
        }
        m.unlock()
        // consume ...
    })

    m.lock()
    items_ready = 1
    c.signal()
    m.unlock()
    thread.join(worker)
}
```

(Reads and writes of globals inside any closure target the real globals.
For **locals**, share via pointers — §2.)

---

## 6. Common patterns

### 6.1 Mutex-protected shared state (from `examples/threads`)

```core
import thread

struct Account {
    balance: i64
    lock: Mutex
    pub func init(v: i64 = 0) { self.balance = v; self.lock.init() }
}

func deposit(acc: ptr<Account>, amount: i64) {
    acc.lock.lock()
    acc.balance += amount
    acc.lock.unlock()
}

func main() {
    acc = Account { }
    acc.init(0)
    pa = &acc                       // share through a pointer (§2)
    w1 = thread.spawn(func() { for i in 0..100 { deposit(pa, 1) } })
    w2 = thread.spawn(func() { for i in 0..100 { deposit(pa, 1) } })
    thread.join(w1)
    thread.join(w2)
    say acc.balance                 // 200
}
```

Note: `acc` here is a **local** of `main`, and `main` joins both threads
before returning — the pointer stays valid for the threads' lifetimes.
If the workers must outlive the spawning scope, heap-allocate the
account.

### 6.2 Atomic counter (lock-free)

```core
import thread

func main() {
    counter = AtomicI32 { }
    worker = thread.spawn(func() {
        for i in 0..1000 { counter.add(1) }
    })
    thread.join(worker)
    say counter.load()                  // 1000
}
```

### 6.3 Work over arrays split by index ranges

```core
import thread

func main() {
    data: [f64; 8] = [0.0; 8]
    half = 4
    w = thread.spawn(func() {
        // reading data is fine here; see the note below before WRITING
        for i in 0..half { say data[i] as i64 }
    })
    thread.join(w)
}
```

Be careful with writes: elements of one array are adjacent bytes; two
threads writing `data[3]` and `data[4]` are a data race by the C11
definition. Copy sub-ranges into per-thread heap buffers and merge
after join.

### 6.4 Shutdown flag with atomics

```core
import thread

func main() {
    run_flag = AtomicBool { }     // false
    w = thread.spawn(func() {
        while !run_flag.load() {
            // poll work; sleep_ms(1) to avoid burning the core
        }
    })
    run_flag.store(true)
    thread.join(w)
}
```

---

## 7. Pitfalls checklist

1. **Assuming captures are shared.** They are copies (§2). Share via
   pointers to heap memory, globals, or `tls`-free statics.
2. **Data races on plain variables.** Every cross-thread access needs a
   lock, an atomic, or happens-before via spawn/join.
3. **Dangling shared state.** A pointer to a spawner's local outlives
   the frame if you don't join (or don't heap-allocate).
4. **Double lock / forgotten unlock.** No RAII, no `defer` — pair every
   `lock` with exactly one `unlock` on every path (including early
   `return`s).
5. **Forgetting `join`.** Leaked handles and threads that may still be
   mutating memory when `main` exits (the process dies mid-flight).
6. **Blocking while holding a lock.** Fine for cond `wait` (it
   releases), deadly for `sleep_ms` or I/O under a mutex others need.
7. **Using `volatile` for threads.** It provides no synchronization
   (memory-model.md §10).
8. **Assuming atomicity of `i64` loads.** On x86-64 aligned 8-byte loads
   are atomic in practice, but Core gives you no guarantee for plain
   loads — use the atomics.
9. **`tls` confusion.** `tls` globals give each thread its own copy;
   they are never shared. Initializing them in one thread does not
   initialize another's.
10. **Calling `process.exit` while others run.** It terminates the
    process immediately; no cleanup, no joins, no flush ordering.
