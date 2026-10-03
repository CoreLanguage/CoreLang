// A bump allocator built on raw memory: classic manual-memory pattern.
import memory

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
        if aligned + n > self.cap {
            panic("bump allocator out of memory")
        }
        self.offset = aligned + n
        unsafe {
            return (self.base + aligned) as ptr<void>
        }
    }
    pub func reset() { self.offset = 0 }
}

func main() {
    bump = Bump { }
    bump.init(1024)
    a: ptr<i32> = unsafe { bump.alloc_raw(4 * 10, 4) as ptr<i32> }
    for i in 0..10 { a[i] = i * 3 }
    say a[7]
    b: ptr<f64> = unsafe { bump.alloc_raw(8 * 4, 8) as ptr<f64> }
    b[0] = 2.5
    say b[0]
    say bump.offset
    bump.reset()
    say bump.offset
}
