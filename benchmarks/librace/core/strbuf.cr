// libcore: string builder
import memory

pub struct StrBuf {
    data: ptr<char>
    len: usize
    cap: usize
    pub func init() { self.data = null; self.len = 0; self.cap = 0 }
    pub func append(s: string) {
        if self.len + len(s) > self.cap {
            mut nc: usize = 64
            while nc < self.len + len(s) { nc = nc * 2 }
            mut nd = memory.alloc_array<char>(nc)
            if self.len > 0 { memcpy(nd, self.data, self.len) }
            self.data = nd
            self.cap = nc
        }
        memcpy(self.data + self.len, c_str(s), len(s))
        self.len += len(s)
    }
    pub func build() -> string {
        // wrap as a string view (caller keeps the buffer alive)
        return str_from_c(self.data)
    }
    pub func size() -> usize { return self.len }
}
