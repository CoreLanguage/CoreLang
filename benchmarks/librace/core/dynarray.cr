// libcore: dynamic array with geometric growth
import memory

pub struct DynArray<T> {
    data: ptr<T>
    len: usize
    cap: usize
    pub func init() { self.data = null; self.len = 0; self.cap = 0 }
    pub func push(v: T) {
        if self.len == self.cap {
            mut nc: usize = 4
            if self.cap > 0 { nc = self.cap * 2 }
            self.data = memory.realloc_array<T>(self.data, nc)
            self.cap = nc
        }
        self.data[self.len] = v
        self.len += 1
    }
    pub func get(i: usize) -> T { return self.data[i] }
    pub func set(i: usize, v: T) { self.data[i] = v }
    pub func size() -> usize { return self.len }
}
