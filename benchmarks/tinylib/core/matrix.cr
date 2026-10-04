// tinylib: dense f64 matrix (row-major) with multiply
import memory

pub struct Mat {
    d: ptr<f64>
    n: usize

    pub func init(n: usize) {
        self.d = memory.alloc_array<f64>(n * n)
        self.n = n
        for i in 0..n * n { self.d[i] = 0.0 }
    }
    pub func set(r: usize, c: usize, v: f64) { self.d[r * self.n + c] = v }
    pub func get(r: usize, c: usize) -> f64 { return self.d[r * self.n + c] }
    // out = self * b (naive triple loop, inner loop over k)
    pub func mul(b: Mat, out: Mat) {
        n = self.n
        for i in 0..n {
            for k in 0..n {
                aik = self.d[i * n + k]
                if aik != 0.0 {
                    for j in 0..n { out.d[i * n + j] += aik * b.d[k * n + j] }
                }
            }
        }
    }
    pub func diag_sum() -> f64 {
        mut s = 0.0
        for i in 0..self.n { s += self.d[i * self.n + i] }
        return s
    }
}
