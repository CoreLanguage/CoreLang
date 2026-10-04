// tinylib: binary min-heap of i64 (array-backed, sift-up/down)
import memory

pub struct MinHeap {
    data: ptr<i64>
    size: usize
    cap: usize

    pub func init() { self.data = null; self.size = 0; self.cap = 0 }

    pub func push(v: i64) {
        if self.size == self.cap {
            mut nc: usize = 16
            if self.cap > 0 { nc = self.cap * 2 }
            self.data = memory.realloc_array<i64>(self.data, nc)
            self.cap = nc
        }
        self.data[self.size] = v
        self.size += 1
        mut i: usize = self.size - 1
        while i > 0 {
            parent = (i - 1) / 2
            if self.data[parent] <= self.data[i] { break }
            t = self.data[parent]
            self.data[parent] = self.data[i]
            self.data[i] = t
            i = parent
        }
    }

    pub func pop() -> i64 {
        top = self.data[0]
        self.size -= 1
        if self.size > 0 {
            self.data[0] = self.data[self.size]
            mut i: usize = 0
            loop {
                l = i * 2 + 1
                r = l + 1
                mut smallest = i
                if l < self.size && self.data[l] < self.data[smallest] { smallest = l }
                if r < self.size && self.data[r] < self.data[smallest] { smallest = r }
                if smallest == i { break }
                t = self.data[i]
                self.data[i] = self.data[smallest]
                self.data[smallest] = t
                i = smallest
            }
        }
        return top
    }
}
