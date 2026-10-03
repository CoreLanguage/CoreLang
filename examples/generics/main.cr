// Generic functions and types, monomorphized at compile time.
struct Pair<A, B> {
    first: A
    second: B
}

func max<T>(a: T, b: T) -> T {
    if a > b { return a }
    return b
}

func swap<T>(a: ptr<T>, b: ptr<T>) {
    t: T = *a
    *a = *b
    *b = t
}

func sum<T>(arr: ptr<T>, n: i32) -> i64 {
    mut total: i64 = 0
    for i in 0..n { total += arr[i] as i64 }
    return total
}

func main() {
    say max(3, 9)
    say max('a', 'z')
    p: Pair<string, i32> = Pair<string, i32> { first: "age", second: 30 }
    say p.first
    say p.second
    x = 1
    y = 2
    swap(&x, &y)
    say x
    say y
    data: [i32; 4] = [1, 2, 3, 4]
    say sum(&data, 4)
}
