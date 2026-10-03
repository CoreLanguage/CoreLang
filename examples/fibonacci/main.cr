// Recursion, iteration, and memoization side by side.
import memory

func fib_recursive(n: i32) -> i32 {
    if n < 2 { return n }
    return fib_recursive(n - 1) + fib_recursive(n - 2)
}

func fib_iterative(n: i32) -> i32 {
    if n < 2 { return n }
    mut a: i32 = 0
    mut b: i32 = 1
    for i in 2..n + 1 {
        mut t = a + b
        a = b
        b = t
    }
    return b
}

func fib_memo(n: i32, memo: ptr<i32>) -> i32 {
    if n < 2 { return n }
    if memo[n] != 0 { return memo[n] }
    memo[n] = fib_memo(n - 1, memo) + fib_memo(n - 2, memo)
    return memo[n]
}

func main() {
    say fib_recursive(20)
    say fib_iterative(30)
    memo = alloc_array<i32>(31)
    say fib_memo(30, memo)
    free(memo)
}
