# tinylib: small-library race

A compact library written in both languages and raced head to head:

- **MinHeap** — array-backed binary min-heap of i64 (sift-up/down), 1M
  random pushes + 1M pops
- **Mat** — dense 160x160 f64 matrix, naive multiply with Core's
  zero-skip inner loop, diagonal checksum
- **parseint** — ASCII <-> i64 conversion, 2M round trips

Same methodology as the other suites: identical workload and identical
LCG streams, same LLVM backend, min of 5 runs, **outputs must match
exactly** (verified by the driver).

```console
$ ./run.sh -O2     # or -O0
```

## Results (this machine, min of 5)

| opt  | Core  | C++   | verdict |
|------|-------|-------|---------|
| -O0  | 0.32s | 0.33s | even    |
| -O2  | 0.16s | 0.15s | even    |

Dead even at both opt levels — the expected outcome for two honest
implementations of the same algorithms handed to the same backend.
The heap is the largest component (~60% of runtime on both sides);
Core's generic-free struct code and C++'s plain struct generate the
same sifting loops.
