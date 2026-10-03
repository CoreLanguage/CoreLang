// ===================================================================
// Core standard library - memory
// Explicit manual memory management. No garbage collector exists:
// every allocation must be matched by an explicit free.
// ===================================================================

extern func core_rt_alloc(size: usize) -> ptr<u8>
extern func core_rt_alloc_zero(size: usize) -> ptr<u8>
extern func core_rt_alloc_aligned(size: usize, align: usize) -> ptr<u8>
extern func core_rt_realloc(p: ptr<u8>, size: usize) -> ptr<u8>
extern func core_rt_free(p: ptr<u8>) -> void
extern func core_rt_memcpy(dst: ptr<void>, src: ptr<void>, n: usize) -> void
extern func core_rt_memmove(dst: ptr<void>, src: ptr<void>, n: usize) -> void
extern func core_rt_memset(dst: ptr<void>, byte: i32, n: usize) -> void
extern func core_rt_memcmp(a: ptr<void>, b: ptr<void>, n: usize) -> i32

// Allocate one T (uninitialized).
pub func alloc<T>() -> ptr<T> {
    unsafe {
        return core_rt_alloc(sizeof(T)) as ptr<T>
    }
}

// Allocate one T, zero-initialized.
pub func alloc_zeroed<T>() -> ptr<T> {
    unsafe {
        return core_rt_alloc_zero(sizeof(T)) as ptr<T>
    }
}

// Allocate space for `count` elements of T.
pub func alloc_array<T>(count: usize) -> ptr<T> {
    unsafe {
        return core_rt_alloc(count * sizeof(T)) as ptr<T>
    }
}

// Allocate zero-initialized space for `count` elements of T.
pub func alloc_zeroed_array<T>(count: usize) -> ptr<T> {
    unsafe {
        return core_rt_alloc_zero(count * sizeof(T)) as ptr<T>
    }
}

// Raw byte allocation.
pub func alloc_bytes(n: usize) -> ptr<u8> { return core_rt_alloc(n) }

// Aligned allocation (align must be a power of two).
pub func alloc_aligned(size: usize, align: usize) -> ptr<u8> {
    return core_rt_alloc_aligned(size, align)
}

// Resize an allocation from alloc_array/realloc_array (element counts).
pub func realloc_array<T>(p: ptr<T>, count: usize) -> ptr<T> {
    unsafe {
        return core_rt_realloc(p as ptr<u8>, count * sizeof(T)) as ptr<T>
    }
}

// Free any allocation obtained from this module. Double frees are
// undefined behavior - see docs/language/memory-model.md.
pub func free<T>(p: ptr<T>) {
    unsafe {
        core_rt_free(p as ptr<u8>)
    }
}

pub func memcpy<T>(dst: ptr<T>, src: ptr<T>, bytes: usize) {
    core_rt_memcpy(dst as ptr<void>, src as ptr<void>, bytes)
}
pub func memset<T>(dst: ptr<T>, byte: u8, bytes: usize) {
    core_rt_memset(dst as ptr<void>, byte as i32, bytes)
}
pub func memcmp(a: ptr<void>, b: ptr<void>, bytes: usize) -> i32 {
    return core_rt_memcmp(a, b, bytes)
}
