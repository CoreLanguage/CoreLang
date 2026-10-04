// Floating point: serially-dependent FP multiply-add chain (FP latency bound,
// cannot vectorize the dependency), plus an FMA-heavy mixed expression.
func main() {
    mut acc: f64 = 0.5
    mut i: u64 = 0
    while i < 200000000 {
        acc = acc * 1.0000000001 + 0.000000001
        i += 1
    }
    say acc > 0.0
}
