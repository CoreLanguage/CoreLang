// Tight loops: nested loops with branches and modulo (branch prediction,
// loop-invariant code motion, division strength reduction). Counts primes.
func main() {
    mut count: i32 = 0
    mut n: i32 = 2
    while n < 1000000 {
        mut isprime: bool = true
        mut d: i32 = 2
        while d * d <= n {
            if n % d == 0 {
                isprime = false
                break
            }
            d += 1
        }
        if isprime { count += 1 }
        n += 1
    }
    say count
}
