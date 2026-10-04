import memory
pub struct DynArray<T> {
    data: ptr<T>
    len: usize
    cap: usize

    pub func push(x: T) {
        if self.cap <= self.len {
            mut nc: usize = 4
            if self.cap > 0 { nc = self.cap * 2 }
            nd: ptr<T> = memory.alloc_array<T>(nc) as ptr<T>
            for i in 0..self.len { nd[i] = self.data[i] }
            if self.cap > 0 { memory.free(self.data) }
            self.data = nd
            self.cap = nc
        }
        self.data[self.len] = x
        self.len = self.len + 1
    }

    pub func get(i: i32) -> T {
        return self.data[i]
    }
}
