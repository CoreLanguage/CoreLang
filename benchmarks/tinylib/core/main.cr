// tinylib driver (Core) - identical workload to cpp/main.cpp
import heap
import matrix
import parseint
import memory

func main() {
    // 1. heap: push 1M pseudo-random, pop all, checksum
    mut h: MinHeap
    h.init()
    mut seed: i64 = 12345
    for i in 0..1000000 {
        seed = seed * 1103515245 + 12345
        h.push(seed)
    }
    mut sum: i64 = 0
    for i in 0..1000000 { sum += h.pop() }
    say sum

    // 2. matrix: 160x160 f64 multiply (same LCG stream as C++)
    n: usize = 160
    mut a: Mat
    a.init(n)
    mut b: Mat
    b.init(n)
    seed = 9876
    for i in 0..n {
        for j in 0..n {
            seed = seed * 1103515245 + 12345
            a.set(i, j, (seed % 1000) as f64 / 1000.0)
            seed = seed * 1103515245 + 12345
            b.set(i, j, (seed % 100) as f64 / 100.0)
        }
    }
    mut c: Mat
    c.init(n)
    a.mul(b, c)
    say c.diag_sum() as i64

    // 3. parse: format 2M numbers to digits, parse them back, checksum
    buf = memory.alloc_array<char>(24)
    seed = 7
    mut total: i64 = 0
    for i in 0..2000000 {
        seed = seed * 1103515245 + 12345
        mut x = seed % 1000000000
        if x < 0 { x = -x }
        mut nd = format_buf(buf, x)
        total += parse_buf(buf, nd)
    }
    say total
}
