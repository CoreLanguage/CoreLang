# Algorithms

Classic algorithms, written the way Core wants them written: pointers for in-place work, fixed arrays for small data, recursion where it's clear, loops where it's fast. Every example here compiles and runs as shown.

## Sorting: quicksort

The canonical pointer-based, in-place sort (see `examples/sorting` in the repo):

```core
func quicksort(arr: ptr<i32>, lo: i32, hi: i32) {
    if lo >= hi { return }
    p = partition(arr, lo, hi)
    quicksort(arr, lo, p - 1)
    quicksort(arr, p + 1, hi)
}

func partition(arr: ptr<i32>, lo: i32, hi: i32) -> i32 {
    pivot = arr[hi]
    mut i = lo - 1
    for mut j in lo..hi {
        if arr[j] < pivot {
            i += 1
            t = arr[i]
            arr[i] = arr[j]
            arr[j] = t
        }
    }
    t = arr[i + 1]
    arr[i + 1] = arr[hi]
    arr[hi] = t
    return i + 1
}
```

`ptr<i32>` + `lo`/`hi` indices is the standard shape: pointer = unchecked access, indices carry the range. Call it with array decay: `quicksort(&data, 0, len(data) - 1)`.

A by-value variant that *returns* a new array (sorting without touching the input):

```core
func bubble(arr: [i32; 6]) -> [i32; 6] {
    mut a = arr                       // copy
    for i in 0..6 {
        for mut j in 0..5 {
            if a[j] > a[j + 1] {
                t = a[j]
                a[j] = a[j + 1]
                a[j + 1] = t
            }
        }
    }
    return a
}
```

## Binary search

```core
func binary_search(arr: ptr<i32>, n: i32, key: i32) -> i32 {
    mut lo: i32 = 0
    mut hi: i32 = n - 1
    while lo <= hi {
        mut mid = (lo + hi) / 2
        if arr[mid] == key { return mid }
        if arr[mid] < key { lo = mid + 1 }
        else { hi = mid - 1 }
    }
    return -1
}
```

Precondition: `arr` is sorted. Returns the index or `-1` — or return `Option<i32>` to make "missing" unmissable (see [error-handling.md](error-handling.md)).

## Recursion: factorial, fibonacci, and a warning

```core
func fact(n: i32) -> i32 {
    if n <= 1 { return 1 }
    return n * fact(n - 1)
}

func fib(n: i32) -> i64 {
    if n < 2 { return n as i64 }
    return fib(n - 1) + fib(n - 2)
}
```

`fib` as written is exponential — memoize into an array for anything beyond n≈35:

```core
func fib_memo(n: i32, memo: ptr<i64>) -> i64 {
    if n < 2 { return n as i64 }
    if memo[n] != 0 { return memo[n] }
    r = fib_memo(n - 1, memo) + fib_memo(n - 2, memo)
    memo[n] = r
    return r
}
```

Core has no tail-call guarantee: convert deep recursions (long lists, big grids) into loops with an explicit stack (see [data-structures.md](data-structures.md)).

## Graphs: BFS and DFS

Adjacency as an `n×n` matrix (row-major: `adj[v * n + u]`), visited flags in a fixed array — no allocation at all:

```core
// BFS: iterative with a ring queue
func bfs(adj: ptr<i32>, n: i32, start: i32) {
    visited: [bool; 8] = [false; 8]
    queue: [i32; 8] = [0; 8]
    mut head = 0
    mut count = 0
    queue[count] = start
    count += 1
    visited[start] = true
    while head < count {
        v = queue[head]
        head += 1
        print(to_string(v) + " ")
        for u in 0..n {
            if adj[v * n + u] == 1 && !visited[u] {
                visited[u] = true
                queue[count] = u
                count += 1
            }
        }
    }
    print("\n")
}

// DFS: recursive
func dfs(adj: ptr<i32>, n: i32, v: i32, visited: ptr<bool>) {
    visited[v] = true
    print(to_string(v) + " ")
    for u in 0..n {
        if adj[v * n + u] == 1 && !visited[u] {
            dfs(adj, n, u, visited)
        }
    }
}
```

## Complete working example

```core
// algorithms.cr - sort, search, and walk a graph
func quicksort(arr: ptr<i32>, lo: i32, hi: i32) {
    if lo >= hi { return }
    p = partition(arr, lo, hi)
    quicksort(arr, lo, p - 1)
    quicksort(arr, p + 1, hi)
}

func partition(arr: ptr<i32>, lo: i32, hi: i32) -> i32 {
    pivot = arr[hi]
    mut i = lo - 1
    for mut j in lo..hi {
        if arr[j] < pivot {
            i += 1
            t = arr[i]
            arr[i] = arr[j]
            arr[j] = t
        }
    }
    t = arr[i + 1]
    arr[i + 1] = arr[hi]
    arr[hi] = t
    return i + 1
}

func binary_search(arr: ptr<i32>, n: i32, key: i32) -> i32 {
    mut lo: i32 = 0
    mut hi: i32 = n - 1
    while lo <= hi {
        mut mid = (lo + hi) / 2
        if arr[mid] == key { return mid }
        if arr[mid] < key { lo = mid + 1 }
        else { hi = mid - 1 }
    }
    return -1
}

func bfs(adj: ptr<i32>, n: i32, start: i32) {
    visited: [bool; 8] = [false; 8]
    queue: [i32; 8] = [0; 8]
    mut head = 0
    mut count = 0
    queue[count] = start
    count += 1
    visited[start] = true
    while head < count {
        v = queue[head]
        head += 1
        print(to_string(v) + " ")
        for u in 0..n {
            if adj[v * n + u] == 1 && !visited[u] {
                visited[u] = true
                queue[count] = u
                count += 1
            }
        }
    }
    print("\n")
}

func dfs(adj: ptr<i32>, n: i32, v: i32, visited: ptr<bool>) {
    visited[v] = true
    print(to_string(v) + " ")
    for u in 0..n {
        if adj[v * n + u] == 1 && !visited[u] {
            dfs(adj, n, u, visited)
        }
    }
}

func main() {
    data: [i32; 10] = [42, 7, 19, 3, 88, 1, 56, 23, 9, 71]
    quicksort(&data, 0, 9)
    for x in data { print(to_string(x) + " ") }   // 1 3 7 9 19 23 42 56 71 88
    print("\n")

    say binary_search(&data, 10, 23)   // 5
    say binary_search(&data, 10, 5)    // -1

    // graph: 0-1, 0-2, 1-3, 2-4
    n = 5
    adj: [i32; 25] = [0; 25]
    adj[0*5+1] = 1; adj[1*5+0] = 1
    adj[0*5+2] = 1; adj[2*5+0] = 1
    adj[1*5+3] = 1; adj[3*5+1] = 1
    adj[2*5+4] = 1; adj[4*5+2] = 1
    bfs(&adj, n, 0)                    // 0 1 2 3 4
    vis: [bool; 8] = [false; 8]
    dfs(&adj, n, 0, &vis)              // 0 1 3 2 4
    print("\n")
}
```

Verified output: `1 3 7 9 19 23 42 56 71 88`, `5`, `-1`, `0 1 2 3 4`, `0 1 3 2 4`.

## Common mistakes

- **`(lo + hi) / 2` overflow** with huge `lo`/`hi` — fine for i32 arrays under 2^30 elements; use `lo + (hi - lo) / 2` for the paranoid version.
- **Forgetting decay.** `quicksort(data, ...)` fails — you must pass `&data` (see [arrays.md](arrays.md)).
- **Uninitialized visited arrays.** `[false; 8]` isn't optional; fresh locals of aggregate type need explicit init in v0.1 style (`mut v: [bool; 8]` then set).
- **Recursion depth.** Quicksort on adversarial inputs (already sorted) hits O(n) depth; shuffle or pick a median pivot for production.
- **Matrix indexing order.** `adj[v * n + u]` — row-major; transposing the operands silently reads the wrong cell.

## Performance notes

- `ptr<T>` indexing in `unsafe` beats bounds-checked array indexing in tight loops — the checks cost a compare+branch each.
- Quicksort: O(n log n) average; insertion-sort small partitions (<16) for real speedups.
- Binary search: O(log n); for tiny arrays linear scans often win (branch prediction).
- Adjacency matrices cost O(n²) memory — fine to n≈4000; use adjacency lists ([data-structures.md](data-structures.md)) for sparse graphs.
- At `-O2`, LLVM vectorizes the bubble-sort inner loop shape and unrolls BFS inner scans; check with `core emit-asm`.

## When to use / not use

- Write the O(n²) version **first** when n is small — clarity wins; optimize with measurements.
- Don't hand-roll hash tables or sorts in application code if a simple O(n log n) sort + linear scan suffices — see [data-structures.md](data-structures.md) for reusable shapes.
- For numeric bulk work, consider SIMD vectors from the `simd` module (lane-wise `+ - * /`) before writing clever scalar code.

Next: [Modules](modules.md).
