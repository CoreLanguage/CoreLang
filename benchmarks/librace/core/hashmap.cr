// libcore: open-addressing hash map (i64 -> i64), linear probing,
// power-of-two capacity, 0.7 load factor.
import memory
import dynarray

pub const HMAP_EMPTY: i64 = -9223372036854775808

pub struct HashMap {
    keys: ptr<i64>
    vals: ptr<i64>
    cap: usize
    size: usize
    pub func init() { self.keys = null; self.vals = null; self.cap = 0; self.size = 0 }
    func hash(k: i64) -> usize {
        mut h = k as u64
        h = h * 0x9E3779B97F4A7C15
        h = h ^ (h >> 32)
        return h as usize
    }
    func grow() {
        mut nc: usize = 16
        if self.cap > 0 { nc = self.cap * 2 }
        oldKeys = self.keys
        oldVals = self.vals
        oldCap = self.cap
        self.keys = memory.alloc_array<i64>(nc)
        self.vals = memory.alloc_array<i64>(nc)
        for i in 0..nc { self.keys[i] = HMAP_EMPTY }
        self.cap = nc
        self.size = 0
        if oldCap > 0 {
            for i in 0..oldCap {
                if oldKeys[i] != HMAP_EMPTY { self.put(oldKeys[i], oldVals[i]) }
            }
            memory.free(oldKeys)
            memory.free(oldVals)
        }
    }
    pub func put(k: i64, v: i64) {
        if self.size * 10 >= self.cap * 7 { self.grow() }
        mut idx = self.hash(k) & (self.cap - 1)
        loop {
            kcur = self.keys[idx]
            if kcur == HMAP_EMPTY {
                self.keys[idx] = k
                self.vals[idx] = v
                self.size += 1
                return
            }
            if kcur == k { self.vals[idx] = v; return }
            idx = (idx + 1) & (self.cap - 1)
        }
    }
    // returns the value, or -1 when absent
    pub func get(k: i64) -> i64 {
        if self.cap == 0 { return -1 }
        mut idx = self.hash(k) & (self.cap - 1)
        loop {
            kcur = self.keys[idx]
            if kcur == HMAP_EMPTY { return -1 }
            if kcur == k { return self.vals[idx] }
            idx = (idx + 1) & (self.cap - 1)
        }
    }
}

