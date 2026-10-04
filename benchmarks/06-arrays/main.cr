// Arrays: sequential + strided indexing with BOUNDS CHECKS (safe Core).
// The volatile base value defeats constant folding; global avoids a big stack frame.
data: [i64; 1000000]
base: i64 = 0

func main() {
    unsafe {
        volatile_store(&base, 3)
    }
    mut i: i64 = 0
    while i < 1000000 {
        data[i] = i * base
        i += 1
    }
    mut sum: i64 = 0
    mut rep: i64 = 0
    while rep < 100 {
        i = 0
        while i < 1000000 {
            sum += data[i]
            i += 1
        }
        i = 0
        while i < 1000000 {
            sum += data[(i * 7) % 1000000]
            i += 1
        }
        rep += 1
    }
    say sum
}
