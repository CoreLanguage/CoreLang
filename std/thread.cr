// ===================================================================
// Core standard library - threads, locks, condition variables, atomics
// Thin, predictable wrappers over POSIX threads. No hidden threads,
// no scheduler, no GC coordination.
// ===================================================================

extern func core_rt_thread_spawn(fn: func()) -> ptr<void>
extern func core_rt_thread_join(handle: ptr<void>) -> void
extern func core_rt_mutex_new() -> ptr<void>
extern func core_rt_mutex_lock(m: ptr<void>) -> void
extern func core_rt_mutex_unlock(m: ptr<void>) -> void
extern func core_rt_mutex_free(m: ptr<void>) -> void
extern func core_rt_rwlock_new() -> ptr<void>
extern func core_rt_rwlock_read(m: ptr<void>) -> void
extern func core_rt_rwlock_write(m: ptr<void>) -> void
extern func core_rt_rwlock_unlock(m: ptr<void>) -> void
extern func core_rt_rwlock_free(m: ptr<void>) -> void
extern func core_rt_cond_new() -> ptr<void>
extern func core_rt_cond_wait(c: ptr<void>, m: ptr<void>) -> void
extern func core_rt_cond_signal(c: ptr<void>) -> void
extern func core_rt_cond_broadcast(c: ptr<void>) -> void
extern func core_rt_cond_free(c: ptr<void>) -> void

// Spawn runs f on a new OS thread and returns an opaque handle.
// join blocks until the thread finishes and releases the handle.
pub func spawn(f: func()) -> ptr<void> { return core_rt_thread_spawn(f) }
pub func join(t: ptr<void>) { core_rt_thread_join(t) }

pub class Mutex {
    handle: ptr<void>
    pub func init() { self.handle = core_rt_mutex_new() }
    pub func lock() { core_rt_mutex_lock(self.handle) }
    pub func unlock() { core_rt_mutex_unlock(self.handle) }
    pub func deinit() { core_rt_mutex_free(self.handle) }
}

pub class RwLock {
    handle: ptr<void>
    pub func init() { self.handle = core_rt_rwlock_new() }
    pub func read() { core_rt_rwlock_read(self.handle) }
    pub func write() { core_rt_rwlock_write(self.handle) }
    pub func unlock() { core_rt_rwlock_unlock(self.handle) }
    pub func deinit() { core_rt_rwlock_free(self.handle) }
}

pub class Cond {
    handle: ptr<void>
    pub func init() { self.handle = core_rt_cond_new() }
    pub func wait(m: Mutex) { core_rt_cond_wait(self.handle, m.handle) }
    pub func signal() { core_rt_cond_signal(self.handle) }
    pub func broadcast() { core_rt_cond_broadcast(self.handle) }
    pub func deinit() { core_rt_cond_free(self.handle) }
}

// Atomic integer with sequentially-consistent operations.
pub class AtomicI32 {
    value: i32
    pub func init() { }
    pub func init(v: i32) { self.value = v }
    pub func load() -> i32 { return atomic_load(&self.value) }
    pub func store(v: i32) { atomic_store(&self.value, v) }
    // Returns the previous value.
    pub func add(v: i32) -> i32 { return atomic_add(&self.value, v) }
    pub func sub(v: i32) -> i32 { return atomic_sub(&self.value, v) }
    pub func swap(v: i32) -> i32 { return atomic_swap(&self.value, v) }
    // Stores new if current == expected; returns the old value.
    pub func compare_exchange(expected: i32, new: i32) -> i32 {
        return atomic_cas(&self.value, expected, new)
    }
}

pub class AtomicI64 {
    value: i64
    pub func init() { }
    pub func init(v: i64) { self.value = v }
    pub func load() -> i64 { return atomic_load(&self.value) }
    pub func store(v: i64) { atomic_store(&self.value, v) }
    pub func add(v: i64) -> i64 { return atomic_add(&self.value, v) }
    pub func sub(v: i64) -> i64 { return atomic_sub(&self.value, v) }
    pub func swap(v: i64) -> i64 { return atomic_swap(&self.value, v) }
    pub func compare_exchange(expected: i64, new: i64) -> i64 {
        return atomic_cas(&self.value, expected, new)
    }
}

pub class AtomicBool {
    value: bool
    pub func load() -> bool { return atomic_load(&self.value) }
    pub func store(v: bool) { atomic_store(&self.value, v) }
    pub func swap(v: bool) -> bool { return atomic_swap(&self.value, v) }
}

pub class AtomicUsize {
    value: usize
    pub func load() -> usize { return atomic_load(&self.value) }
    pub func store(v: usize) { atomic_store(&self.value, v) }
    pub func add(v: usize) -> usize { return atomic_add(&self.value, v) }
    pub func sub(v: usize) -> usize { return atomic_sub(&self.value, v) }
    pub func swap(v: usize) -> usize { return atomic_swap(&self.value, v) }
    pub func compare_exchange(expected: usize, new: usize) -> usize {
        return atomic_cas(&self.value, expected, new)
    }
}
